"""The versions of a verification (workspace.py): the copy of the host project (what is copied,
linked, skipped), the folders never linked, the commands of the builds, the cache by commit."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fakes  # noqa: E402
import workspace  # noqa: E402

SCRIPTS = Path(__file__).resolve().parents[3] / "terrain_unreal" / "Scripts"


class Folders(unittest.TestCase):
    def test_the_scripts_of_the_plugin_write_to_terrain_engine(self):
        self.assertEqual(workspace.written_content_folders(SCRIPTS), {"TerrainEngine"})

    def test_never_linked(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            self.assertEqual(workspace.never_linked(m, SCRIPTS), {"TerrainEngine", "TerrainTest"})


class Copy(unittest.TestCase):
    def test_copied_linked_skipped(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root, linkContent=["BigPack"])
            links = []
            target = Path(root) / "copy"
            done = workspace.copy_host_project(
                m, target, {"TerrainEngine", "TerrainTest"},
                linker=lambda source, link: links.append((source, link)))
            self.assertTrue((target / "TerrainSandbox.uproject").is_file())
            self.assertTrue((target / "Config" / "DefaultEngine.ini").is_file())
            self.assertTrue((target / "Content" / "TerrainTest" / "TerrainGasp.umap").is_file())
            self.assertTrue((target / "Content" / "TerrainEngine" / "M_TerrainWater.uasset")
                            .is_file())
            self.assertTrue((target / "Plugins" / "Other" / "Other.uplugin").is_file())
            for skipped in ("Binaries", "Intermediate", "Saved", "DerivedDataCache",
                            "TerrainSandbox.sln", "Plugins/terrain-engine",
                            "Plugins/Other/Binaries", "Content/BigPack"):
                self.assertFalse((target / skipped).exists(), skipped)
            self.assertEqual(len(links), 1)
            self.assertEqual(links[0][1], target / "Content" / "BigPack")
            self.assertIn("linked Content/BigPack/", "\n".join(done))

    def test_a_folder_written_to_is_never_linked(self):
        for name in ("TerrainEngine", "TerrainTest"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as root:
                m = fakes.a_machine(root, linkContent=["BigPack", name])
                target = Path(root) / "copy"
                with self.assertRaises(workspace.WorkspaceError) as caught:
                    workspace.copy_host_project(m, target, {"TerrainEngine", "TerrainTest"},
                                                linker=lambda s, t: None)
                self.assertIn(name, str(caught.exception))
                self.assertFalse(target.exists())  # refused before anything is copied

    @unittest.skipIf(os.name == "nt", "symbolic links need rights under Windows")
    def test_removal_keeps_what_links_point_to(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root, linkContent=["BigPack"])
            cache = m.work_root / "cache" / "x" / "project"
            workspace.copy_host_project(m, cache, {"TerrainEngine", "TerrainTest"})
            self.assertTrue((cache / "Content" / "BigPack").is_symlink())
            workspace.remove_tree(m.work_root / "cache" / "x", m.work_root)
            self.assertFalse((m.work_root / "cache" / "x").exists())
            self.assertTrue((m.project.parent / "Content" / "BigPack" / "T_Rock.uasset")
                            .is_file())
            with self.assertRaises(workspace.WorkspaceError):
                workspace.remove_tree(m.project.parent, m.work_root)


class Commands(unittest.TestCase):
    def test_build_commands(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            c = workspace.build_commands(m, "P/TerrainSandbox.uproject", "P/Plugins/te", "T",
                                         "terrain_probe", system="Windows")
            self.assertTrue(str(c["plugin"][0]).endswith(os.path.join("BatchFiles", "Build.bat")))
            self.assertEqual(c["plugin"][1:5], ["TerrainSandboxEditor", "Win64", "Development",
                                                "-Project=P/TerrainSandbox.uproject"])
            self.assertIn("-DTERRAIN_REPO=P/Plugins/te", c["configure"])
            self.assertEqual(c["tools"][-2:], ["terrain_probe", "terrain_make_planet"])

    def test_probe_target(self):
        with tempfile.TemporaryDirectory() as root:
            core = Path(root) / "terrain_core"
            core.mkdir()
            self.assertIsNone(workspace.probe_target(root))
            (core / "CMakeLists.txt").write_text("add_executable(terrain_probe_water x.cpp)\n")
            self.assertEqual(workspace.probe_target(root), "terrain_probe_water")
            (core / "CMakeLists.txt").write_text("add_executable(terrain_probe tools/probe.cpp)\n")
            self.assertEqual(workspace.probe_target(root), "terrain_probe")


class Cache(unittest.TestCase):
    def test_built_once_by_commit(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            runner = fakes.FakeRunner()
            v = workspace.prepare(m, "main", "avant", runner)
            self.assertTrue(v.built_now)
            self.assertEqual(v.sha, fakes.SHA)
            self.assertEqual(v.probe_kind, "probe")
            self.assertTrue(v.probe.name.startswith("terrain_probe"))
            self.assertTrue(v.features["loadReport"])
            self.assertTrue((v.cache / workspace.MARKER).is_file())
            self.assertTrue((v.project_dir / "Content" / "TerrainEngine").is_dir())
            builds = [c for c in runner.commands if c[0].endswith("Build.bat") or
                      c[0].endswith("Build.sh")]
            self.assertEqual(len(builds), 1)
            again = workspace.prepare(m, "main", "avant", runner)
            self.assertFalse(again.built_now)
            builds = [c for c in runner.commands if c[0].endswith(("Build.bat", "Build.sh"))]
            self.assertEqual(len(builds), 1)  # the cache of the commit
            fresh = workspace.prepare(m, "main", "avant", runner, fresh=True)
            self.assertTrue(fresh.built_now)

    def test_a_failed_build_is_never_kept(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root)
            with self.assertRaises(workspace.WorkspaceError):
                workspace.prepare(m, "main", "avant", fakes.FakeRunner(build_ok=False))
            self.assertFalse((m.work_root / "cache" / fakes.SHA / workspace.MARKER).exists())
            v = workspace.prepare(m, "main", "avant", fakes.FakeRunner())
            self.assertTrue(v.built_now)

    def test_pull_request_refs(self):
        runner = fakes.FakeRunner()
        workspace.resolve(runner, "mirror.git", "#57", None)
        self.assertIn("refs/pull/57/head^{commit}", runner.commands[-1])


if __name__ == "__main__":
    unittest.main()
