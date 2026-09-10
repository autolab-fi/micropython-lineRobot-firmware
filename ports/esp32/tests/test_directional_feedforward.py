"""Host-side checks for direction-specific motor feed-forward."""

import ast
from pathlib import Path


SOURCE = Path(__file__).parents[1] / "modules" / "lineRobot.py"


def load_feedforward_method():
    tree = ast.parse(SOURCE.read_text(encoding="utf-8"))
    robot = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "Robot")
    method = next(
        node for node in robot.body
        if isinstance(node, ast.FunctionDef) and node.name == "_apply_directional_feedforward"
    )
    module = ast.Module(body=[method], type_ignores=[])
    namespace = {}
    exec(compile(ast.fix_missing_locations(module), str(SOURCE), "exec"), namespace)
    return namespace["_apply_directional_feedforward"]


class StubRobot:
    @staticmethod
    def constrain(value, minimum, maximum):
        return max(minimum, min(maximum, value))


def test_directional_feedforward():
    apply = load_feedforward_method()
    robot = StubRobot()

    assert apply(robot, 100, 20, 16, 6) == 116
    assert apply(robot, -100, -20, 16, 6) == -106
    assert apply(robot, -20, 20, 16, 6) == -20
    assert apply(robot, 20, -20, 16, 6) == 20
    assert apply(robot, 0, 20, 16, 6) == 16
    assert apply(robot, 0, 0, 16, 6) == 0
    assert apply(robot, 995, 20, 16, 6) == 1000


if __name__ == "__main__":
    test_directional_feedforward()
