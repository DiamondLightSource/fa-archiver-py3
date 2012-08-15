% ids = fa_name2id(names)
%
% Returns FA ids for names (either one string or a cell array of strings)

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
function ids = fa_name2id(names)
    [allnames, allids] = fa_getids();

    % Unfortunately, a single string will be a 'char array' while multiple
    % strings will come as a cell array, so to make sure we can live with both,
    % we check and make it a cell array if it is not already.
    if iscell(names)
        names_c = names;
    else
        names_c{1} = names;
    end

    for n = 1:length(names_c)
        f = find(strcmp(names_c(n), allnames));
        if length(f) > 1
            error(['More than one matching FA ID for "' names_c{n} '"'])
        elseif isempty(f)
            error(['Could not find FA ID for "' names_c{n} '"'])
        else
            ids(n) = allids(f);
        end
    end
end
