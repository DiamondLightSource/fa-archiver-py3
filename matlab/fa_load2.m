% d = fa_load(tse, mask [, type [,server]])
%
% Grab bpm data from the FA archiver
%
% Input:
%   tse = [start_time end_time]
%   mask = vector containing FA ids [1 2 3], or [78 79] etc.
%   type = 'F' for full data
%          'd' for 10072/64 decimated
%          'D' for 10072/16384 decimated
%          'C' for continuous data, in which case tse must be a single number
%              specifying a duration in seconds
%   server = IP address of FA archiver
%
% Output:
%   d = object containing all of the data, containing the following fields:
%
%   d.decimation    Decimation factor corresponding to requested type
%   d.f_s           Sample rate of captured data at selected decimation
%   d.timestamp     Timestamp (in Matlab format) of first point
%   d.ids           Array of FA ids, copy of mask
%   d.gapix         Array of start of segment pointer
%   d.gaptimes      Array of segment start times
%   d.data          The returned data
%   d.t             Timestamp array for each sample point
%   d.day           Matlab number of day containing first sample
%
% FA ids are returned in the same order that they were requested, including
% any duplicates.

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

function d = fa_load2(tse, mask, type, server)
    import java.nio.IntBuffer;

    % Process arguments
    if nargin < 3
        type = 'F';
    end
    if nargin < 4
        server = 'fa-archiver.diamond.ac.uk';
        server = '172.23.240.12';
    end
    [decimation, frequency, typestr] = process_type(server, type);

    % Compute unique sorted list from id mask and remember the permuation.
    [request_mask, dummy, perm] = unique(mask);
    id_count = length(request_mask);

    % Prepare the request and send to server
    offset = get_tz_offset(tse(1));
    maskstr = sprintf('%d,', request_mask);
    maskstr = maskstr(1:end-1);     % Chop off trailing comma
    request = sprintf('R%sM%sT%sZET%sZNATE', typestr, maskstr, ...
        format_time(tse(1) - offset), format_time(tse(2) - offset));
    [sc, cleanup] = send_request(server, request);

    % Check the error code response and raise an error if failed
    buf = read_bytes(sc, 1);
    rc = buf.get();
    if rc ~= 0
        % On error the entire response is the error message.
        [buf, len] = read_bytes(sc, 1024);
        error([char(rc) buf_as_string(buf, len)]);
    end

    % First response back is the number of samples in complete buffer followed
    % by an initial timestamp header followed by the header for the first block.
    header_buf = read_bytes(sc, 16);
    sample_count = header_buf.getLong();    % Total sample count requested
    block_size   = header_buf.getInt();     % Number of samples per block
    block_offset = header_buf.getInt();     % Offset into first block read

    % Prepare the result data structure
    d = {};
    d.t = zeros(1, sample_count);
    d.decimation = decimation;
    d.f_s = frequency;
    d.ids = request_mask;
    if decimation == 1
        field_count = 1;
        data = zeros(2, id_count, sample_count);
    else
        field_count = 4;
        data = zeros(2, 4, id_count, sample_count);
    end

    % Read the requested data block by block
    samples_read = 0;
    first_block = true;
    while samples_read < sample_count
        % Timestamp buffer first
        timestamp_buf = read_bytes(sc, 12);
        timestamp = timestamp_buf.getLong();
        duration  = timestamp_buf.getInt();

        if first_block
            first_block = false;
            [d.day, d.timestamp, ts_offset, scaling] = ...
                process_first_timestamp(offset, timestamp);
        end

        % Work out how many samples in the next block
        block_count = block_size - block_offset;
        if samples_read + block_count > sample_count
            block_count = sample_count - samples_read;
        end

        % Read the data and convert to matlab format.
        data_buf = read_bytes(sc, 8 * field_count * id_count * block_count);
        int_buf = IntBuffer.allocate(2 * field_count * id_count * block_count);
        int_buf.put(data_buf.asIntBuffer());
        if decimation == 1
            data(:, :, samples_read + 1:samples_read + block_count) = ...
                reshape(int_buf.array(), 2, id_count, block_count);
        else
            data(:, :, :, samples_read + 1:samples_read + block_count) = ...
                reshape(int_buf.array(), 2, 4, id_count, block_count);
        end

        % Compute timestamps vector
        d.t(samples_read + 1:samples_read + block_count) = ...
            scaling * ((timestamp - ts_offset) + ...
                (block_offset + (0:block_count - 1)) * duration / block_size);

        block_offset = 0;
        samples_read = samples_read + block_count;
    end

    % Restore the originally requested permutation if necessary.
    if any(diff(perm) ~= 1)
        d.ids = d.ids(perm);
        if decimation == 1
            d.data = data(:, perm, :);
        else
            d.data = data(:, :, perm, :);
        end
    else
        d.data = data;
    end
end


function [sock, cleanup] = send_request(server, request)
    import java.nio.channels.SocketChannel;
    import java.net.InetSocketAddress;
    import java.lang.String;
    import java.nio.ByteBuffer;

    [server, port] = strtok(server, ':');
    if port; port = int32(str2num(port(2:end))); else port = 8888; end

    sock = SocketChannel.open();
    sock.connect(InetSocketAddress(server, port));
    cleanup = onCleanup(@() close_socket(sock.socket()));

    request = ByteBuffer.wrap(String(request).getBytes('US-ASCII'));
    sock.write(request);

    newline = ByteBuffer.allocate(1);
    newline.put(10);
    newline.flip();
    sock.write(newline);
end


function close_socket(s)
    s.close();
end


% Reads a block of the given number of bytes from the socket
function [buf, pos] = read_bytes(sc, count)
    import java.nio.ByteBuffer;
    import java.nio.ByteOrder;

    buf = ByteBuffer.allocate(count);
    buf.order(ByteOrder.LITTLE_ENDIAN);
    while buf.remaining() ~= 0
        if sc.read(buf) < 0
            break
        end
    end
    pos = buf.position();
    buf.flip();
end


function s = buf_as_string(buf, len)
    bytes = buf.array();
    s = char(bytes(1:len))';
end


% Reads decimation and frequency parameters from server
function [first_dec, second_dec, frequency] = read_params(server)
    [sock, cleanup] = send_request(server, 'CdDF');
    [buf, len] = read_bytes(sock, 1024);

    params = textscan(buf_as_string(buf, len), '%f');
    first_dec  = params{1}(1);
    second_dec = params{1}(2);
    frequency  = params{1}(3);
end


function [decimation, frequency, typestr] = process_type(server, type)
    [first_dec, second_dec, frequency] = read_params(server);

    if type == 'F' || type == 'C'
        decimation = 1;
        typestr = 'F';
    elseif type == 'd'
        decimation = first_dec;
        typestr = 'D';
        frequency = frequency / decimation;
    elseif type == 'D'
        decimation = first_dec * second_dec;
        typestr = 'DD';
        frequency = frequency / decimation;
    else
        error('Invalid datatype requested');
    end
end


% Converts matlab timestamp into ISO 8601 format as expected by fa-capture.
function str = format_time(time)
    format = 'yyyy-mm-ddTHH:MM:SS';
    str = datestr(time, format);
    % Work out the remaining unformatted fraction of a second
    delta_s = 3600 * 24 * (time - datenum(str, format));
    if delta_s > 0.9999; delta_s = 0.9999; end      % Fudge for last fraction
    if 0 < delta_s
        nano = sprintf('%.4f', delta_s);
        str = [str nano(2:end)];
    end
end


% Returns the offset to be subtracted from matlab time to get UTC time.
function offset = get_tz_offset(time)
    import java.util.TimeZone;

    % Convert the time to UTC by subtracting the time zone specific offset.
    % Unfortunately getting the correct daylight saving offset is a bit of a
    % stab in the dark as we should be asking in local time.
    scaling = 1 / (1e3 * 3600 * 24);    % Java time in milliseconds
    epoch = 719529;                     % 1970-01-01 in matlab time
    local_tz = TimeZone.getDefault();
    java_time = (time - epoch) / scaling - local_tz.getRawOffset();
    offset = local_tz.getOffset(java_time) * scaling;
end


% Computes parameters for processing the 64-bit microseconds archiver timestamps
function [day, time, ts_offset, scaling] = ...
        process_first_timestamp(offset, timestamp)
    scaling = 1 / (1e6 * 3600 * 24);    % Archiver time in microseconds
    epoch = 719529;                     % 1970-01-01 in matlab time
    time = timestamp * scaling + epoch + offset;
    day = floor(time);
    ts_offset = (day - epoch - offset) / scaling;
end
