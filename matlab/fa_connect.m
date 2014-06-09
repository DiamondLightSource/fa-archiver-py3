% c = fa_connect(server)
%
% Makes socket connection to server and provides functions for sending commands
% and receiving a response.
%
% The following methods can be called once a connection has been established:
%
%   c.send_command(command)
%       Sends command string terminated with newline character to server
%   c.read_bytes(count, require)
%       Reads up to count bytes from server, fails if require is set and fewer
%       bytes are received.
%   c.read_int_array(count)
%       Reads array of count 32-bit integers from server
%   c.read_long_array(count)
%       Reads array of count 64-bit integers from server
%   c.read_string([count])
%       Reads string from server.

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

function c = fa_connect(server, port)
    import java.nio.channels.SocketChannel;
    import java.net.InetSocketAddress;

    if ~exist('port', 'var'); port = 8888; end

    c = {};

    % Open the channel and connect to the server.
    c.channel = SocketChannel.open();
    c.channel.connect(InetSocketAddress(server, port));

    % Ensure that the socket is closed when no longer needed.
    socket = c.channel.socket();
    c.cleanup = onCleanup(@() socket.close());

    % Populate the structure with all the functions we're going to provide.
    c.send_command = @(request) send_command(c, request);
    c.read_bytes = @(count, require) read_bytes(c, count, require);
    c.read_int_array = @(count) read_int_array(c, count);
    c.read_long_array = @(count) read_long_array(c, count);
    c.read_string = @(varargin) read_string(c, varargin{:});
end

function send_command(c, request)
    import java.lang.String;
    import java.nio.ByteBuffer;

    % Send request with newline termination.
    request = ByteBuffer.wrap(String([request 10]).getBytes('US-ASCII'));
    c.channel.write(request);
end

% Reads a block of the given number of bytes from the socket
function [buf, pos] = read_bytes(c, count, require)
    import java.nio.ByteBuffer;
    import java.nio.ByteOrder;

    buf = ByteBuffer.allocate(count);
    buf.order(ByteOrder.LITTLE_ENDIAN);
    while buf.remaining() ~= 0
        if c.channel.read(buf) < 0
            break
        end
    end
    pos = buf.position();
    if require && pos ~= count
        throw(MException( ...
            'fa_load:read_bytes', 'Too few bytes received from server'))
    end
    buf.flip();
end

function a = read_int_array(c, count)
    import java.nio.IntBuffer;

    buf = read_bytes(c, 4 * count, true);
    ints = IntBuffer.allocate(count);
    ints.put(buf.asIntBuffer());
    a = double(ints.array());
end

function a = read_long_array(c, count)
    import java.nio.LongBuffer;

    buf = read_bytes(c, 8 * count, true);
    longs = LongBuffer.allocate(count);
    longs.put(buf.asLongBuffer());
    a = longs.array();
end

function s = read_string(c, count)
    if ~exist('count', 'var'); count = 4096; end
    [buf, len] = read_bytes(c, count, false);
    bytes = buf.array();
    s = char(bytes(1:len))';
end
