import subprocess
import sys

from fa import __version__


def test_cli_version():
    cmd = [sys.executable, "-m", "fa", "--version"]
    assert subprocess.check_output(cmd).decode().strip() == __version__
