"""Locate the shared TomCat automation client without duplicating its schemas."""
import os
from pathlib import Path
import runpy
import sys

directory = os.environ.get("TOMCAT_AUTOMATION_HOME")
if not directory:
    raise SystemExit("Set TOMCAT_AUTOMATION_HOME to the repository's Tools/TomCatMCP directory.")
directory = Path(directory).resolve()
if not (directory / "client.py").is_file():
    raise SystemExit("TOMCAT_AUTOMATION_HOME does not contain client.py.")
sys.path.insert(0, str(directory))
runpy.run_path(str(directory / "client.py"), run_name="__main__")
