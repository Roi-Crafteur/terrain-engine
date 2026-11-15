"""The graph of the materials a script of terrain_unreal/Scripts builds, without Unreal (agent 40):
a fake module unreal that keeps every object the script gets back, so that the nodes, their
properties (parameter_name, code, the inputs of a Custom node), the links between them and the
outputs of each material can be read after the run. Used by water_horizon_scripts_test.py to prove
that a switch of the look is wired: its parameter reaches a Custom node by the input of its name,
the code of that node reads it, and that node reaches an output of the material.
"""

import collections
import re
import sys


class GraphUnreal:
    """Any attribute is another fake, any call is recorded with the object it was made on and
    gives a new fake back, except the few answers the scripts read."""

    def __init__(self, graph, path="unreal", owner=None):
        self._graph = graph
        self._path = path
        self._owner = owner

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return GraphUnreal(self._graph, f"{self._path}.{name}", self)

    def __call__(self, *args, **kwargs):
        leaf = self._path.rsplit(".", 1)[-1]
        if leaf == "get_num_material_expressions":
            return 0
        if leaf == "does_asset_exist":
            return False
        result = GraphUnreal(self._graph, f"{self._path}()")
        self._graph.append((leaf, self._owner, args, result))
        return result

    def path(self):
        return self._path


Node = collections.namedtuple("Node", "kind material properties inputs")


class Graph:
    """The nodes of each material of a run, their links and the outputs they reach."""

    def __init__(self, calls):
        self.names = {}  # material object -> its asset name
        self.nodes = {}  # node object -> Node
        self.links = []  # (source node, source output, target node, target input)
        self.outputs = set()  # the nodes wired to a property of their material
        properties = collections.defaultdict(dict)
        for leaf, owner, args, result in calls:
            if leaf == "create_asset":
                self.names[result] = args[0]
            elif leaf == "create_material_expression":
                kind = args[1].path().rsplit(".", 1)[-1]
                self.nodes[result] = Node(kind, args[0], properties[result], [])
            elif leaf == "set_editor_property" and owner is not None:
                properties[owner][args[0]] = args[1]
            elif leaf == "connect_material_expressions":
                self.links.append((args[0], args[1], args[2], args[3]))
            elif leaf == "connect_material_property":
                self.outputs.add(args[0])
        for node in self.nodes.values():
            for item in node.properties.get("inputs", []):
                node.inputs.append(properties[item].get("input_name"))

    def material_of(self, name):
        return [m for m, n in self.names.items() if n == name]

    def parameters(self, material, name):
        return [obj for obj, node in self.nodes.items() if node.material is material and
                node.properties.get("parameter_name") == name]

    def reaches_output(self, start):
        """Whether the node start reaches an output of its material, link after link."""
        seen, todo = {start}, [start]
        while todo:
            node = todo.pop()
            if node in self.outputs:
                return True
            for source, _, target, _ in self.links:
                if source is node and target not in seen:
                    seen.add(target)
                    todo.append(target)
        return False

    def wired(self, material, name):
        """The parameter name of material wired: one of its nodes linked to the input name of a
        Custom node whose code reads name, which reaches an output (a node handed every scalar
        without reading this one does not count). Returns the reasons it is not, empty when it
        is."""
        params = self.parameters(material, name)
        if not params:
            return [f"no parameter {name}"]
        reasons = []
        good = 0
        for param in params:
            readers = [t for s, _, t, i in self.links if s is param and i == name and
                       self.nodes.get(t, Node("", None, {}, [])).kind ==
                       "MaterialExpressionCustom"]
            if not readers:
                reasons.append(f"{name} reaches no input {name} of a Custom node")
            for reader in readers:
                node = self.nodes[reader]
                code = re.sub(r"//[^\n]*", "", node.properties.get("code", ""))
                if name not in node.inputs or not re.search(rf"\b{name}\b", code):
                    reasons.append(f"the node {node.properties.get('description')} does not "
                                   f"read {name}")
                elif not self.reaches_output(reader):
                    reasons.append(f"the node {node.properties.get('description')} reaches no "
                                   "output of the material")
                else:
                    good += 1
        return [] if good > 0 else reasons

    def unplugged(self, material, name):
        """A copy of this graph with the links of the parameter name of material removed."""
        copy = Graph([])
        copy.names, copy.nodes, copy.outputs = self.names, self.nodes, self.outputs
        params = self.parameters(material, name)
        copy.links = [link for link in self.links if not any(link[0] is p for p in params)]
        return copy


def run(folder, script):
    """Runs a script of the materials as the editor does (its own globals, every asset new): the
    graph it built, and the error it raised if any."""
    calls = []
    sys.modules["unreal"] = GraphUnreal(calls)
    path = folder / script
    scope = {"__name__": "__main__", "__file__": str(path), "__builtins__": __builtins__}
    error = None
    try:
        exec(compile(path.read_text(encoding="utf-8"), str(path), "exec"), scope)
    except Exception as failure:  # noqa: BLE001  (any failure of the script is the finding)
        error = f"{script}: {type(failure).__name__}: {failure}"
    return Graph(calls), error
