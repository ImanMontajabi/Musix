"""The type scale is a set of roles, not a set of numbers.

Material's type styles carry a line height, a letter spacing and a weight as
well as a size, and SungText works out which role a style is from the size it
was given. That leaves 14 ambiguous between titleSmall, bodyMedium and
labelLarge, which are three different trackings, so a size written as a literal
is a role picked by a heuristic rather than stated. Naming the role removes the
guess, and this keeps them named.
"""

import pathlib
import re
import unittest

QML = pathlib.Path(__file__).resolve().parents[1] / "qml"
LITERAL = re.compile(r"font\.pixelSize\s*:\s*\d")


class TypeScaleTest(unittest.TestCase):
    def test_no_literal_type_sizes(self):
        offenders = []
        for source in sorted(QML.glob("*.qml")):
            for number, line in enumerate(source.read_text().splitlines(), 1):
                if LITERAL.search(line):
                    offenders.append(f"{source.name}:{number}")
        self.assertEqual(
            offenders,
            [],
            "a type size has to name its role on Theme, because a number does "
            "not carry the role's line height, tracking or weight: "
            + ", ".join(offenders),
        )


if __name__ == "__main__":
    unittest.main()
