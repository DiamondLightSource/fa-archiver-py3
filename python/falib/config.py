# Processing bpm configuration files

# Copyright (c) 2011 Michael Abbott, Diamond Light Source Ltd.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# Contact:
#      Dr. Michael Abbott,
#      Diamond Light Source Ltd,
#      Diamond House,
#      Chilton,
#      Didcot,
#      Oxfordshire,
#      OX11 0DE
#      michael.abbott@diamond.ac.uk

import os
import re
import glob

import falib


__all__ = ['load_location_file', 'compute_bpm_groups']



def compute_bpm_groups(fa_id_list, groups, patterns):
    '''Computes the list of groups and BPM/FA mappings to be presented to the
    user.  The format of the computed list is:
        [(group_name, [(bpm_name, bpm_id)])]
    Ie, a list of group names, and for each group a list of bpm name and id
    pairs.'''
    group_dict = dict((group, []) for group in groups)
    for id, bpm in fa_id_list:
        for match, pattern, replace in patterns:
            if re.match(match, bpm):
                key = re.sub(pattern, replace, bpm)
                group_dict[key].append((bpm, id))
                break
    return [('Other', [])] + [(group, group_dict[group]) for group in groups]


def find_nearby_file(module, filename, file_pattern, full_path = False):
    '''Returns path to the filename.  If full_path is False the path is relative
    to the directory containing the file named module and file_pattern is used
    to construct the file name, otherwise filename is returned unchanged.'''
    if full_path:
        return filename
    else:
        return os.path.realpath(os.path.join(
            os.path.dirname(module), file_pattern % filename))

def find_location_file(location, full_path):
    '''Computes full path to given location file.'''
    return find_nearby_file(
        __file__, location, '../conf/%s.conf', full_path)

def list_location_files():
    '''Returns list of configured location files.'''
    return [
        re.sub('.*/([^/]*)\.conf', r'\1', conf)
        for conf in glob.glob(
            os.path.join(os.path.dirname(__file__), '..', 'conf', '*.conf'))]


def load_location_file(globs, location, full_path, server = None, port = None):
    result = dict(FA_PORT = falib.DEFAULT_PORT)
    config_file = find_location_file(location, full_path)
    context = dict(here = os.path.dirname(config_file), os = os)
    execfile(config_file, context, result)
    if server:
        result['FA_SERVER'] = server
    if port:
        result['FA_PORT'] = port
    globs.update(result)
