# Processing bpm configuration files

import os
import re


__all__ = ['load_bpm_defs']


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

def load_bpm_defs(location, full_path):
    '''Loads BPM definitions for the specified location.  Returns the name and
    port of the server and the appropriate group pattern list.'''
    if not full_path:
        location = os.path.join(
            os.path.dirname(__file__), '..', 'conf', '%s.conf' % location)
    bpm_defs = {}
    execfile(location, {}, bpm_defs)
    groups = compute_bpm_groups(
        bpm_defs['BPM_LIST'], bpm_defs['GROUPS'], bpm_defs['PATTERNS'])
    return bpm_defs['FA_SERVER'], bpm_defs.get('FA_PORT', 8888), groups

