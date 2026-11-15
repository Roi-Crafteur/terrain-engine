"""The HLSL the material scripts hand to their Custom nodes, in order, without Unreal (agent 40,
material_graph_support.py): the proof that a split of a script of the look (temporary_terrain_hlsl.py
first, over 500 lines) changes nothing of the materials. Run it before and after the split: the two
outputs must be equal, and the sha256 it prints too.

Usage: python3 tests/hlsl_snapshot.py <repository> > before.hlsl
"""

import hashlib
import pathlib
import sys

SCRIPTS = ("create_temporary_terrain_material.py", "create_horizon_gpu_material.py",
           "create_water_materials.py")


def main(root):
    root = pathlib.Path(root)
    sys.path[:0] = [str(root / "terrain_unreal/Scripts"), str(pathlib.Path(__file__).parent)]
    import material_graph_support as graphs  # noqa: E402  (after the path)
    out = []
    for script in SCRIPTS:
        graph, error = graphs.run(root / "terrain_unreal/Scripts", script)
        if error is not None:
            print(error, file=sys.stderr)
            return 1
        for node in graph.nodes.values():
            if "code" in node.properties:
                out.append(f"// {script}: {node.properties.get('description')}\n"
                           f"{node.properties['code']}")
    text = "\n".join(out)
    print(text)
    print(f"// sha256 {hashlib.sha256(text.encode()).hexdigest()} ({len(out)} Custom nodes)",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
