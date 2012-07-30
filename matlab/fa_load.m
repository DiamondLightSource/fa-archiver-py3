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
%              specifying the number of samples wanted
%          Use 'Z' suffix to select ID0 capture as well.
%   server = IP address of FA archiver
%
% Output:
%   d = object containing all of the data, containing the following fields:
%
%   d.decimation    Decimation factor corresponding to requested type
%   d.f_s           Sample rate of captured data at selected decimation
%   d.timestamp     Timestamp (in Matlab format) of first point
%   d.ids           Array of FA ids, copy of mask
%   d.data          The returned data
%   d.t             Timestamp array for each sample point
%   d.day           Matlab number of day containing first sample
%   d.id0           ID 0 values if Z option specified.
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

function d = fa_load(tse, mask, type, server)
    % Process arguments
    if nargin < 3
        type = 'F';
    end
    if nargin < 4
        server = 'fa-archiver.diamond.ac.uk';
    end
    [decimation, frequency, typestr, ts_at_end, save_id0] = ...
        process_type(server, type);

    % Compute unique sorted list from id mask and remember the permuation.
    [request_mask, dummy, perm] = unique(mask);
    id_count = length(request_mask);

    % Prepare the request and send to server
    maskstr = sprintf('%d,', request_mask);
    maskstr = maskstr(1:end-1);     % Chop off trailing comma
    if save_id0; id0_req = 'Z'; else id0_req = ''; end
    if type == 'C'
        % Continuous data request
        tz_offset = get_tz_offset(now);
        request = sprintf('S%sTE%s', maskstr, id0_req);
    else
        tz_offset = get_tz_offset(tse(1));
        if ts_at_end; ts_req = 'A'; else ts_req = 'E'; end
        request = sprintf('R%sM%sT%sZET%sZNAT%s%s', typestr, maskstr, ...
            format_time(tse(1) - tz_offset), ...
            format_time(tse(2) - tz_offset), ts_req, id0_req);
    end
    [sc, cleanup] = send_request(server, request);

    % Check the error code response and raise an error if failed
    buf = read_bytes(sc, 1, true);
    rc = buf.get();
    if rc ~= 0
        % On error the entire response is the error message.
        error([char(rc) read_string(sc)]);
    end

    if type == 'C'
        % For continuous data tse is the count.
        sample_count = tse(1);
    else
        % For historical data get the sample count at the head of the response.
        sample_count = read_long_array(sc, 1);
    end
    % Read the timestamp header with block size and initial offset.
    header = read_int_array(sc, 2);
    block_size = header(1);             % Number of samples per block
    initial_offset = header(2);         % Offset into first block read

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

    % Prepeare for reading.  Alas we need to support both options for timestamps
    % if we want to support C data, as C data cannot be delivered with
    % timestamps at end.
    if ts_at_end
        block_offset = 0;
        read_block_size = 65536;
    else
        block_offset = initial_offset;
        read_block_size = block_size;

        % Prepare timestamps buffers, we guess a sensible initial size
        timestamps = zeros(round(sample_count / block_size) + 2, 1);
        durations  = zeros(round(sample_count / block_size) + 2, 1);
        if save_id0
            id_zeros = int32(zeros(round(sample_count / block_size) + 2, 1));
        end
        ts_read = 0;
    end
    if save_id0; ts_buf_size = 16; else ts_buf_size = 12; end

    % Read the requested data block by block
    samples_read = 0;
    while samples_read < sample_count
        if ~ts_at_end
            % Timestamp buffer first unless we've put it off until the end.
            timestamp_buf = read_bytes(sc, ts_buf_size, true);
            timestamps(ts_read + 1) = timestamp_buf.getLong();
            durations (ts_read + 1) = timestamp_buf.getInt();
            if save_id0
                id_zeros  (ts_read + 1) = timestamp_buf.getInt();
            end
            ts_read = ts_read + 1;
        end

        % Work out how many samples in the next block
        block_count = read_block_size - block_offset;
        if samples_read + block_count > sample_count
            block_count = sample_count - samples_read;
        end

        % Read the data and convert to matlab format.
        int_buf = read_int_array(sc, 2 * field_count * id_count * block_count);
        if decimation == 1
            data(:, :, samples_read + 1:samples_read + block_count) = ...
                reshape(int_buf, 2, id_count, block_count);
        else
            data(:, :, :, samples_read + 1:samples_read + block_count) = ...
                reshape(int_buf, 2, 4, id_count, block_count);
        end

        block_offset = 0;
        samples_read = samples_read + block_count;
    end

    if ts_at_end
        % Timestamps at end are sent as a count followed by all the timestamps
        % together followed by all the durations together.
        ts_read = read_int_array(sc, 1);
        timestamps = read_long_array(sc, ts_read);
        durations  = read_int_array(sc, ts_read);
        if save_id0
            id_zeros   = read_int_array(sc, ts_read);
        end
    end

    [d.day, d.timestamp, d.t] = process_timestamps( ...
        timestamps, durations, ...
        tz_offset, sample_count, block_size, initial_offset);
    if save_id0
        d.id0 = process_id0( ...
            id_zeros, sample_count, block_size, initial_offset, decimation);
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


% Opens socket channel to given server and sends request.  Both the opened
% socket and a cleanup handler are returned, the socket will be closed when
% cleanup is discarded.
function [channel, cleanup] = send_request(server, request)
    import java.nio.channels.SocketChannel;
    import java.net.InetSocketAddress;
    import java.lang.String;
    import java.nio.ByteBuffer;

    % Allow for a non standard port to be specified as part of the server name.
    [server, port] = strtok(server, ':');
    if port; port = str2num(port(2:end)); else port = 8888; end

    % Open the channel and connect to the server.
    channel = SocketChannel.open();
    channel.connect(InetSocketAddress(server, port));

    % Ensure that the socket is closed when no longer needed.
    socket = channel.socket();
    cleanup = onCleanup(@() socket.close());

    % Send request with newline termination.
    request = ByteBuffer.wrap(String([request 10]).getBytes('US-ASCII'));
    channel.write(request);
end


% Reads a block of the given number of bytes from the socket
function [buf, pos] = read_bytes(sc, count, require)
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
    if require && pos ~= count
        error('Too few bytes received from server');
    end
    buf.flip();
end

function a = read_int_array(sc, count)
    import java.nio.IntBuffer;

    buf = read_bytes(sc, 4 * count, true);
    ints = IntBuffer.allocate(count);
    ints.put(buf.asIntBuffer());
    a = double(ints.array());
end

function a = read_long_array(sc, count)
    import java.nio.LongBuffer;

    buf = read_bytes(sc, 8 * count, true);
    longs = LongBuffer.allocate(count);
    longs.put(buf.asLongBuffer());
    a = double(longs.array());
end

function s = read_string(sc)
    [buf, len] = read_bytes(sc, 4096, false);
    bytes = buf.array();
    s = char(bytes(1:len))';
end


% Reads decimation and frequency parameters from server
function [first_dec, second_dec, frequency] = read_params(server)
    [sock, cleanup] = send_request(server, 'CdDF');
    params = textscan(read_string(sock), '%f');
    first_dec  = params{1}(1);
    second_dec = params{1}(2);
    frequency  = params{1}(3);
end


% Process decimation request in light of server parameters.
function [decimation, frequency, typestr, ts_at_end, save_id0] = ...
        process_type(server, type)

    [first_dec, second_dec, frequency] = read_params(server);

    save_id0 = type(end) == 'Z';
    if save_id0; type = type(1:end-1); end

    ts_at_end = false;
    if strcmp(type, 'F') || strcmp(type, 'C')
        decimation = 1;
        typestr = 'F';
    elseif strcmp(type, 'd')
        decimation = first_dec;
        typestr = 'D';
        frequency = frequency / decimation;
    elseif strcmp(type, 'D')
        decimation = first_dec * second_dec;
        typestr = 'DD';
        frequency = frequency / decimation;
        ts_at_end = true;
    else
        error('Invalid datatype requested');
    end
end


% Converts matlab timestamp into ISO 8601 format as expected by FA server.
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
function tz_offset = get_tz_offset(time)
    import java.util.TimeZone;

    % Convert the time to UTC by subtracting the time zone specific offset.
    % Unfortunately getting the correct daylight saving offset is a bit of a
    % stab in the dark as we should be asking in local time.
    scaling = 1 / (1e3 * 3600 * 24);    % Java time in milliseconds
    epoch = 719529;                     % 1970-01-01 in matlab time
    local_tz = TimeZone.getDefault();
    java_time = (time - epoch) / scaling - local_tz.getRawOffset();
    tz_offset = local_tz.getOffset(java_time) * scaling;
end


% Computes timebase from timestamps and offsets.
% Note that we could avoid converting timestamps to doubles until subtracting
% ts_offset below, but in fact we have just enough precision for microseconds.
function [day, start_time, ts] = process_timestamps( ...
        timestamps, durations, ...
        tz_offset, sample_count, block_size, initial_offset)

    scaling = 1 / (1e6 * 3600 * 24);    % Archiver time in microseconds
    epoch = 719529;                     % 1970-01-01 in matlab time
    start_time = timestamps(1) * scaling + epoch + tz_offset;
    day = floor(start_time);

    ts_offset = (day - epoch - tz_offset) / scaling;
    timestamps = scaling * (timestamps - ts_offset);
    durations = (scaling * durations) * [0:block_size - 1] / block_size;

    ts = repmat(timestamps, 1, block_size) + durations;
    ts = reshape(ts', [], 1);
    ts = ts(initial_offset + 1:initial_offset + sample_count);
end


% Computes id0 array from captured information.
function id0 = process_id0( ...
        id_zeros, sample_count, block_size, initial_offset, decimation)
    id_zeros = int32(id_zeros);
    offsets = int32(decimation * [0:block_size - 1]);
    id0 = repmat(id_zeros, 1, block_size) + repmat(offsets, size(id_zeros), 1);
    id0 = reshape(id0', [], 1);
    id0 = id0(initial_offset + 1:initial_offset + sample_count);
end
