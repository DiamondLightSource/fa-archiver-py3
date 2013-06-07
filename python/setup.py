from setuptools import setup
        
# these lines allow the version to be specified in Makefile.private
import os
version = os.environ.get("MODULEVER", "0.0")
        
setup(
#    install_requires = ['\cothread'], # require statements go here
    name = 'fa-archiver',
    version = version,
    description = 'Diamond Fast Archiver',
    author = 'Michael Abbott',
    author_email = 'Michael.Abbott@diamond.ac.uk',
    packages = ['audio','falib','lines','viewer', 'conf'],
    package_data = { 'conf' : 
                     [ 'BR.conf', 
                       'fa-ids.sr', 
                       'SR.conf', 
                       'TEST.conf',
                       'TS.conf' ],
                     'viewer' : [ 'viewer.ui' ]
                     },  
    install_requires = [ 'cothread==2.8', 'numpy==1.6.2' ],
    entry_points = {'console_scripts': [
            'fa-viewer = viewer.fa_viewer:main',
            'fa-audio = audio.audio:main']}, 
#    include_package_data = True, # use this to include non python files
    zip_safe = False
    )        
