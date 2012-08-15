% [names, ids] = fa_getids()
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
function [names, ids] = fa_getids()
    if isunix
        fa_ids_file = '/home/ops/diagnostics/concentrator/fa-ids.sr';
    else
        fa_ids_file = fullfile(fileparts(mfilename('fullpath')), 'fa-ids.sr');
    end
    [allids, allnames] = ...
        textread(fa_ids_file, '%n %s', 'commentstyle', 'shell');
    valid = ~strcmp(allnames, '');
    ids = allids(valid);
    names = allnames(valid);
end
