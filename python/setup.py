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
    packages = [
        'fa_archiver',
        'fa_archiver.audio', 'fa_archiver.falib', 'fa_archiver.viewer',
        'conf'],
    package_data = {
        'conf' : ['BR.conf', 'SR.conf', 'TS.conf'],
        'fa_archiver.viewer' : ['viewer.ui']},
    install_requires = ['cothread', 'numpy'],
    entry_points = {
        'console_scripts': [
            'fa-viewer = fa_archiver.viewer.fa_viewer:main',
            'fa-audio = fa_archiver.audio.audio:main']},
    zip_safe = False)
