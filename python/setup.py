from setuptools import setup

# these lines allow the version to be specified in Makefile.private
import os
version = os.environ.get("MODULEVER", "0.0")

setup(
    name = 'fa-archiver',
    version = version,
    description = 'Diamond Fast Archiver',
    author = 'Michael Abbott',
    author_email = 'Michael.Abbott@diamond.ac.uk',
    packages = ['audio', 'falib', 'viewer', 'conf'],
    package_data = {
        'conf' : ['BR.conf', 'SR.conf', 'TS.conf'],
        'viewer' : ['viewer.ui']},
    install_requires = ['cothread', 'numpy'],
    entry_points = {
        'console_scripts': [
            'fa-viewer = viewer.fa_viewer:main',
            'fa-audio = audio.audio:main']},
    zip_safe = False)
