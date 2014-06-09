% [names, ids, stored, x_names, y_names] = ...
%     fa_getids([server,] ['stored',] ['missing'])
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
function [names, ids, stored, x_names, y_names] = fa_getids(varargin)
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
    else
        entry = get_names_ids(server, port);
        names_table(name) = entry;
    end
    stored = entry{1};
    ids = entry{2};
    x_names = entry{3};
    y_names = entry{4};
    names = entry{5};

    % If requested only return names for stored ids
    if do_stored
        names = names(stored);
        ids = ids(stored);
        x_names = x_names(stored);
        y_names = y_names(stored);
        stored = stored(stored);
    end

    % If requested synthesise missing names
    missing = strmatch('', names, 'exact');
    if do_missing
        if ~isempty(missing)
            [new_names, new_x, new_y] = synthesize_names(ids(missing));
            names(missing) = new_names;
            x_names(missing) = new_x;
            y_names(missing) = new_y;
        end
    else
        present = setdiff(1:length(names), missing);
        names = names(present);
        ids = ids(present);
        stored = stored(present);
        x_names = x_names(present);
        y_names = y_names(present);
    end
end


function [names, x_names, y_names] = synthesize_names(ids)
    % Alas it doesn't seem to be possible to format directly into an array, so
    % we need to use textscan() to prepare the new names.
    names = textscan(sprintf('FA-ID-%d\n', ids), '%s');
    names = names{1};
    x_names = repmat({'X'}, length(ids), 1);
    y_names = repmat({'Y'}, length(ids), 1);
end


% Go to given server for list of names and ids.
function fields = get_names_ids(server, port)
    c = tcp_connect(server, port);
    c.write_string(['CL' 10]);
    response = c.read_string(65536);

    if strcmp(response, ['Unknown command' 10])
        warning('Synthesising FA id names for server %s:%d', server, port);
        fields = create_fields(server, port);
    else
        % The format of the response is quite exact and somewhat tailored to
        % help Matlab:
        %   id description x_name y_name
        % If the description is missing the a single - is returned, and no field
        % is completely absent.
        fields = textscan(response, ...
            ['%c%d%*[ ]%[^ ]%*[ ]%[^ ]%*[ ]%[^' 10 ']'], ...
            'Whitespace', '', 'ReturnOnError', 0);
        fields{1} = fields{1} == '*';    % Archived flag
    end
end


% Fallback action if server doesn't provide names, instead synthesise names
% covering the available range of ids.
function fields = create_fields(server, port)
    c = tcp_connect(server, port);
    c.write_string(['CK' 10]);
    id_count = textscan(c.read_string(), '%d');
    id_count = id_count{1};

    ids = (1:id_count-1)';
    [names, x_names, y_names] = synthesize_names(ids);
    stored = true(id_count-1, 1);
    fields = { stored, ids, x_names, y_names, names };
end
