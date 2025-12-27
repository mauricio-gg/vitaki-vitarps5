from __future__ import print_function

INDENT_WIDTH = 4
current = 0

class indent():
    def __enter__(self):
        global current
        current += INDENT_WIDTH

    def __exit__(self, *args, **kwargs):
        global current
        current -= INDENT_WIDTH


def iprint(str=""):
    print(" " * current, end="")
    print(str)
