[![CI](https://github.com/DiamondLightSource/fa-archiver-py3/actions/workflows/ci.yml/badge.svg)](https://github.com/DiamondLightSource/fa-archiver-py3/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/DiamondLightSource/fa-archiver-py3/branch/main/graph/badge.svg)](https://codecov.io/gh/DiamondLightSource/fa-archiver-py3)
[![PyPI](https://img.shields.io/pypi/v/fa-archiver.svg)](https://pypi.org/project/fa-archiver)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://www.apache.org/licenses/LICENSE-2.0)

# fa-archiver-py3

Python tools for accessing the diamond fast acquisition archiver

Contains a tool to view fast archiver data (fa_viewer) and a tool to listen to the data
in audo format (fa-audio). It also contains a library which can be used to access
data from the fast archiver web server.


What            | Where
:---:           | :---:
Source          | <https://github.com/DiamondLightSource/fa-archiver-py3>
PyPI            | `pip install fa-archiver`
Docker          | `docker run ghcr.io/diamondlightsource/fa-archiver-py3:latest`
Releases        | <https://github.com/DiamondLightSource/fa-archiver-py3/releases>

```python
from fa import __version__

print(f"Hello fa {__version__}")
```

Check fa-archiver version:

```
python -m fa --version
```
