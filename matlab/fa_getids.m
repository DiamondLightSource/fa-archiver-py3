% [names, ids] = fa_getids([all_names])
%
% Returns device names and corresponding communication controller ids.  Used
% internally for all name <-> id conversion.

% Copyright (c) 2012 Michael Abbott, Diamond Light Source Ltd.
%
% This program is free software; you can redistribute it and/or modify
% it under the terms of the GNU General Public License as published by
% the Free Software Foundation; either version 2 of the License, or
% (at your option) any later version.
%
% This program is distributed in the hope that it will be useful,
% but WITHOUT ANY WARRANTY; without even the implied warranty of
% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
% GNU General Public License for more details.
%
% You should have received a copy of the GNU General Public License
% along with this program; if not, write to the Free Software
% Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
%
% Contact:
%      Dr. Michael Abbott,
%      Diamond Light Source Ltd,
%      Diamond House,
%      Chilton,
%      Didcot,
%      Oxfordshire,
%      OX11 0DE
%      michael.abbott@diamond.ac.uk
function [names, ids] = fa_getids(all_names)
    if isunix
        fa_ids_file = '/home/ops/diagnostics/concentrator/fa-ids.sr';
    else
        fa_ids_file = fullfile(fileparts(mfilename('fullpath')), 'fa-ids.sr');
    end
    [ids, names] = textread(fa_ids_file, '%n %s', 'commentstyle', 'shell');

    % Unless all names requested filter out both names and IDs with no name
    % assigned.
    if nargin == 0; all_names = false; end
    if ~all_names
        valid = ~strcmp(names, '');
        ids = ids(valid);
        names = names(valid);
    end
end
