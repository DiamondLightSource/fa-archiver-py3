% [names, ids, stored] = fa_getids([server,] ['stored',] ['missing'])
%
% Returns device names and corresponding communication controller ids.  Used
% internally for all name <-> id conversion.  The stored array records for each
% id whether it is stored in the archiver database.
%
% The server argument can be omitted (in which case the default is 'SR'), can be
% a physical server address with an optional port, or can be one of 'SR', 'BR'
% or 'TS'.
%
% If 'stored' is passed as an argument then IDs with names but which are not
% stored in the archiver will be filtered out.
%
% If 'missing' is passed as an argument then names will be synthesised for any
% ids for which a name was not given.

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
function [names, ids, stored] = fa_getids(varargin)
    % Create a persistent table mapping server names to FA id lookup lists.
    % This will avoid a visit to the server for every name lookup.
    persistent names_table;
    if isempty(names_table); names_table = containers.Map(); end

    % Consume the keyword arguments so we can pass what's left to fa_find_server
    do_stored  = strmatch('stored', varargin, 'exact');
    varargin(do_stored) = [];
    do_missing = strmatch('missing', varargin, 'exact');
    varargin(do_missing) = [];

    [name, server, port] = fa_find_server(varargin{:});
    if isKey(names_table, name)
        % If we've already seen this request then return our cached table.
        entry = names_table(name);
        names = entry{1};
        ids = entry{2};
        stored = entry{3};
    else
        [names, ids, stored] = get_names_ids(server, port);
        names_table(name) = { names, ids, stored };
    end

    % If requested only return names for stored ids
    if do_stored
        names = names(stored);
        ids = ids(stored);
    end

    % If requested synthesise missing names
    if do_missing
        missing = strmatch('', names, 'exact');
        % Alas it doesn't seem to be possible to format directly into an array,
        % so we need to use textscan() to prepare the new names.
        new_names = textscan(sprintf('FA-ID-%d\n', ids(missing)), '%s');
        names(missing) = new_names{1};
    end
end


% Go to given server for list of names and ids.
function [names, ids, stored] = get_names_ids(server, port)
    c = tcp_connect(server, port);
    c.write_string(['CL' 10]);
    response = c.read_string(65536);

    if strcmp(response, ['Unknown command' 10])
        warning('Synthesising FA id names for server %s:%d', ...
            server, port);
        [names, ids, stored] = synthesise_names(server, port);
    else
        fields = textscan(response, ['%c%d%*c%[^' 10 ']'], 'Whitespace', '');
        stored = fields{1} == '*';  % * marks fields stored in archiver database
        ids    = fields{2};
        names  = fields{3};
    end
end


% Fallback action if server doesn't provide names, instead synthesise names
% covering the available range of ids.
function [names, ids, stored] = synthesise_names(server, port)
    c = tcp_connect(server, port);
    c.write_string(['CK' 10]);
    id_count = textscan(c.read_string(), '%d');
    id_count = id_count{1};

    ids = (1:id_count-1)';
    names = textscan(sprintf('FA-ID-%d\n', ids), '%s');
    names = names{1};
    stored = boolean(ones(id_count-1, 1));
end
