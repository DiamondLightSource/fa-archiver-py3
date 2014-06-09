% c = tcp_connect(server, port, [timeout])
%
% Makes socket connection to server and provides functions for sending commands
% and receiving a response.  If a timeout is specified the socket will be
% configured into non blocking mode and reads will end when timeout occurs.
%
% The following methods can be called once a connection has been established:
%
%   c.write_string(command)
%       Sends command string to server.  No termination is added.
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
function sock = tcp_connect(server, port, timeout)
    import java.nio.channels.SocketChannel;
    import java.net.InetSocketAddress;
    import java.nio.channels.Selector;
    import java.nio.channels.SelectionKey;

    sock = {};

    % Open the channel and connect to the server.
    sock.channel = SocketChannel.open();
    sock.channel.connect(InetSocketAddress(server, port));

    % Ensure that the socket is closed when no longer needed.
    sock.cleanup = onCleanup(@() close_socket(sock));

    if exist('timeout', 'var')
        sock.timeout = timeout;

        % Enable use of select so we can detect timeouts
        sock.selector = Selector.open();
        sock.channel.configureBlocking(false);
        sock.key = sock.channel.register(sock.selector, SelectionKey.OP_READ);
    else
        sock.timeout = [];
    end

    % Assign bound functions
    sock.write_string   = @(varargin) write_string(sock, varargin{:});
    sock.read_string    = @(varargin) read_string(sock, varargin{:});
    sock.read_bytes     = @(varargin) read_bytes(sock, varargin{:});
    sock.read_int_array = @(varargin) read_int_array(sock, varargin{:});
    sock.read_long_array = @(varargin) read_long_array(sock, varargin{:});
end


% Called when parent sock object is released
function close_socket(sock)
    socket = sock.channel.socket();
    socket.close()
end


function write_string(sock, string)
    import java.lang.String;
    import java.nio.ByteBuffer;

    message = ByteBuffer.wrap(String(string).getBytes('US-ASCII'));
    sock.channel.write(message);
end


% Reads a block of the given number of bytes from the socket
function [buf, pos] = read_bytes(sock, count, require, timeout)
    import java.nio.ByteBuffer;
    import java.nio.ByteOrder;

    if exist('timeout', 'var')
        assert(~isempty(sock.timeout), 'Connection not configured for timeout');
    else
        timeout = sock.timeout;
    end

    sc = sock.channel;

    buf = ByteBuffer.allocate(count);
    buf.order(ByteOrder.LITTLE_ENDIAN);
    while buf.remaining() ~= 0
        if ~isempty(timeout)
            sock.selector.select(timeout);
        end
        rx = sc.read(buf);
        if rx <= 0
            break
        end
    end
    pos = buf.position();
    if require && pos ~= count
        error('Too few bytes received from server');
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

function string = read_string(sock, count, varargin)
    if ~exist('count', 'var'); count = 4096; end
    [buf, len] = read_bytes(sock, count, false, varargin{:});
    bytes = buf.array();
    string = char(bytes(1:len))';
end
