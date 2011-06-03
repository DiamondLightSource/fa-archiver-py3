# Processing bpm configuration files

import os
import re
import glob

import falib


__all__ = ['load_location_file', 'compute_bpm_groups', 'load_bpm_list']


def load_bpm_list(filename):
    '''Loads list of ids and bpms from given file.'''
    for line in file(filename).readlines():
        if line and line[0] != '#':
            id_bpm = line.split()
            if len(id_bpm) == 2:
                id, bpm = id_bpm
                yield (int(id), bpm)

def compute_bpm_groups(filename, groups, patterns):
    '''Computes the list of groups and BPM/FA mappings to be presented to the
    user.  The format of the computed list is:
        [(group_name, [(bpm_name, bpm_id)])]
    Ie, a list of group names, and for each group a list of bpm name and id
    pairs.'''
    group_dict = dict((group, []) for group in groups)
    for id, bpm in load_bpm_list(filename):
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
        return os.path.join(
            os.path.dirname(module), file_pattern % filename)

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

def load_config(config_file):
    '''Loads given configuration file.'''
    config = {}
    execfile(config_file, {}, config)
    return config


def load_bpm_defs(location, full_path = False, server = None):
    '''Loads BPM definitions for the specified location.  Returns the name and
    port of the server and the appropriate group pattern list.'''
    bpm_defs = load_config(find_location_file(location, full_path))
    groups = compute_bpm_groups(
        bpm_defs['BPM_LIST'], bpm_defs['GROUPS'], bpm_defs['PATTERNS'])
    if server:
        bpm_defs['FA_SERVER'] = server
    return bpm_defs['FA_SERVER'], bpm_defs.get('FA_PORT', 8888), groups


def load_location_file(globs, location, full_path, server = None):
    globs['FA_PORT'] = falib.DEFAULT_PORT
    execfile(find_location_file(location, full_path), {}, globs)
    if server:
        globs['FA_SERVER'] = server
