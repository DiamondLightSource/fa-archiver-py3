% [names, x_names, y_names] = fa_id2name(ids, [args])
%
% Returns corresponding EPICS names for a vector of FA ids
% See fa_getids for the possible optional arguments.

% Copyright (c) 2011 Michael Abbott, Diamond Light Source Ltd.
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
function [names, x_names, y_names] = fa_id2name(ids, varargin)
    [all_names, all_ids, s, all_x_names, all_y_names] = fa_getids(varargin{:});

    names   = lookup_names(ids, all_ids, all_names, '');
    x_names = lookup_names(ids, all_ids, all_x_names, 'X');
    y_names = lookup_names(ids, all_ids, all_y_names, 'Y');

    % An annoying little hack: handling cells for single values causes trouble.
    if length(names) == 1
        names = cell2mat(names);
        x_names = cell2mat(x_names);
        y_names = cell2mat(y_names);
    end
end

function result = lookup_names(ids, all_ids, values, default)
    index(1:max(ids)+1) = {default};
    index(all_ids+1) = values;
    result = index(ids+1);
end
