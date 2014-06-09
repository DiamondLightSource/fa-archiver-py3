% [name, server, port] = fa_find_server(server)
%
% Converts server name into a canonical form.

% Copyright (c) 2014 Michael Abbott, Diamond Light Source Ltd.
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
function [name, server, port] = fa_find_server(server)
    if ~exist('server', 'var'); server = 'SR'; end

    % Check for name in our list of server names
    persistent server_names;
    if isempty(server_names)
        server_names = containers.Map();

        % Load default server names from external file.  This makes name
        % maintenance easier by avoiding buring defaults in the middle of code.
        fa_ids_file = ...
            fullfile(fileparts(mfilename('fullpath')), 'server_names');
        [names, servers, ports] = ...
            textread(fa_ids_file, '%s %s %d', 'commentstyle', 'shell');
        for ix = 1:length(names)
            server_names(names{ix}) = { servers{ix}, ports(ix), false };
        end
    end
    if isKey(server_names, server);
        % This is a short name for the server and we know about this one.
        name = server;
        server_entry = server_names(name);
        server  = server_entry{1};
        port    = server_entry{2};
        checked = server_entry{3};
        if ~checked
            reported_name = server_name(server, port);
            if strcmp(reported_name, name) ||  strcmp(reported_name, '')
                server_names(name) = { server, port, true };
            else
                warning('Server %s (%s:%d) reports as %s', ...
                    name, server, port, reported_name);
                server_names(reported_name) = { server, port, true };
            end
        end
    else
        % We don't know this server, it had better be a proper machine name.
        % Extract the port part of the server name with appropriate default.
        [server, port] = strtok(server, ':');
        if port; port = str2double(port(2:end)); else port = 8888; end
        name = server_name(server, port);
        server_names(name) = { server, port, true };
    end
end

function name = server_name(server, port)
    c = tcp_connect(server, port);
    c.write_string(['CN' 10]);
    name = strtok(c.read_string(), char(10));
    if strcmp(name, 'Unknown command'); name = ''; end
end
