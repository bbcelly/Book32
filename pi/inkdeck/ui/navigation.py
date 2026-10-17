"""The screens InkDeck can show."""

from enum import Enum


class Screen(Enum):
    MAIN_MENU = "main_menu"
    LIBRARY = "library"
    READER = "reader"
    IP_SCANNER = "ip_scanner"
    TODO = "todo"
    KLIPPER = "klipper"
    TYPEWRITER = "typewriter"
