% d = fa_load(tse, mask [, type [, server [, show_bar]])
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
%          'CD' for continuous decimated data, and similarly tse must specify
%              number of samples
%          Use 'Z' suffix to select ID0 capture as well.
%       Default is 'F'.
%   server = IP address of FA archiver or one of 'SR', 'BR', 'TS'.
%       Default is 'SR'.
%   show_bar = can be set to false to hide progress bar.  Default is true.
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

function d = fa_load(tse, mask, type, server, show_bar)
    % Assign defaults
    if ~exist('type', 'var');       type = 'F'; end
    if ~exist('server', 'var');     server = 'SR'; end
    if ~exist('show_bar', 'var');   show_bar = true; end

    % Start by normalising the server name
    [name, server, port] = fa_find_server(server);

    % Parse arguments
    [decimation, frequency, typestr, ts_at_end, save_id0, max_id] = ...
        process_type(server, port, type);

    % Compute unique sorted list from id mask and remember the permuation.
    [request_mask, dummy, perm] = unique(mask);
    id_count = length(request_mask);

    % Prepare the request and send to server
    [request, tz_offset] = format_server_request( ...
        request_mask, max_id, save_id0, typestr, tse, decimation, ts_at_end);

    % Send formatted request to server.  This returns open connection to server
    % for reading response and returned datastream.
    sc = tcp_connect(server, port);
    sc.write_string([request 10]);

    % Parse response including initial header.
    [sample_count, block_size, initial_offset] = ...
        read_server_response(sc, typestr, tse);

    % Capture requested data.
    simple_data = strcmp(typestr, 'C')  ||  decimation == 1;
    [data, timestamps, durations, id_zeros, sample_count] = ...
        read_data(sc, sample_count, id_count, block_size, initial_offset, ...
            ts_at_end, save_id0, simple_data, show_bar);

    % Prepare final result structure.  This involves some interpretation of the
    % captured timestamp information.
    d = format_results( ...
        decimation, frequency, request_mask, perm, ...
        data, timestamps, durations, id_zeros, ...
        tz_offset, save_id0, simple_data, sample_count, ...
        block_size, initial_offset);
end


% Process decimation request in light of server parameters.  This involves an
% initial parameter request to the server.
%   Note that the ts_at_end flag for DD data is an important optimisation;
% without this, data capture takes an unreasonably long time.
function [decimation, frequency, typestr, ts_at_end, save_id0, max_id] = ...
        process_type(server, port, type)

    % Read decimation and frequency parameters from server
    sc = tcp_connect(server, port);
    sc.write_string(['CdDFKC' 10]);
    params = textscan(sc.read_string(), '%f');
    first_dec  = params{1}(1);
    second_dec = params{1}(2);
    frequency  = params{1}(3);
    max_id     = params{1}(4);
    stream_dec = params{1}(5);

    % Parse request
    save_id0 = type(end) == 'Z';
    if save_id0; type = type(1:end-1); end

    ts_at_end = false;
    if strcmp(type, 'F') || strcmp(type, 'C')
        decimation = 1;
        typestr = type;
    elseif strcmp(type, 'CD')
        if stream_dec == 0; error('No decimated data from server'); end
        decimation = stream_dec;
        typestr = 'C';
    elseif strcmp(type, 'd')
        decimation = first_dec;
        typestr = 'D';
    elseif strcmp(type, 'D')
        decimation = first_dec * second_dec;
        typestr = 'DD';
        ts_at_end = true;
    else
        error('Invalid datatype requested');
    end
    frequency = frequency / decimation;
end


function [request, tz_offset] = format_server_request( ...
        request_mask, max_id, save_id0, typestr, tse, decimation, ts_at_end)

    maskstr = format_mask(request_mask, max_id);
    if save_id0; id0_req = 'Z'; else id0_req = ''; end
    if typestr == 'C'
        % Continuous data request
        tz_offset = get_tz_offset(now);
        request = sprintf('S%sTE%s', maskstr, id0_req);
        if decimation > 1; request = [request 'D']; end
    else
        tz_offset = get_tz_offset(tse(2));
        if ts_at_end; ts_req = 'A'; else ts_req = 'E'; end
        request = sprintf('R%sM%sT%sZET%sZNAT%s%s', typestr, maskstr, ...
            format_time(tse(1) - tz_offset), ...
            format_time(tse(2) - tz_offset), ts_req, id0_req);
    end
end


% Reads initial response from server including timestamp header, raises error if
% server rejects request.
function [sample_count, block_size, initial_offset] = ...
        read_server_response(sc, typestr, tse)

    % Check the error code response and raise an error if failed
    buf = sc.read_bytes(1, true);
    rc = buf.get();
    if rc ~= 0
        % On error the entire response is the error message.
        error([char(rc) sc.read_string()]);
    end

    if typestr == 'C'
        % For continuous data tse is the count.
        sample_count = tse(1);
    else
        % For historical data get the sample count at the head of the response.
        sample_count = double(sc.read_long_array(1));
    end

    % Read the timestamp header with block size and initial offset.
    header = sc.read_int_array(2);
    block_size = header(1);             % Number of samples per block
    initial_offset = header(2);         % Offset into first block read
end


% Prepares arrays and flags for reading data from server.
function [ ...
        field_count, block_offset, read_block_size, ...
        data, timestamps, durations, id_zeros] = ...
    prepare_data( ...
        sample_count, id_count, simple_data, initial_offset, block_size, ...
        ts_at_end, save_id0)

    if simple_data
        field_count = 1;
        data = zeros(2, id_count, sample_count);
    else
        field_count = 4;
        data = zeros(2, 4, id_count, sample_count);
    end

    % Prepare for reading.  Alas we need to support both options for timestamps
    % if we want to support C data, as C data cannot be delivered with
    % timestamps at end.
    if ts_at_end
        block_offset = 0;
        read_block_size = 65536;
        % Need dummy values for values read at end
        timestamps = 0;
        durations = 0;
        id_zeros = 0;
    else
        block_offset = initial_offset;
        read_block_size = block_size;

        % Prepare timestamps buffers, we guess a sensible initial size.  We use
        % 64 bit integers for timestamps to avoid premature loss of accuracy.
        timestamps = zeros(round(sample_count / block_size) + 2, 1, 'int64');
        durations  = zeros(round(sample_count / block_size) + 2, 1);
        if save_id0
            id_zeros = int32(zeros(round(sample_count / block_size) + 2, 1));
        else
            id_zeros = 0;   % Dummy value
        end
    end
end


% Reads data from server.
function [data, timestamps, durations, id_zeros, sample_count] = ...
    read_data( ...
        sc, sample_count, id_count, block_size, initial_offset, ...
        ts_at_end, save_id0, simple_data, show_bar)

    function truncate_data()
        warning('Data truncated');
        sample_count = samples_read;
        if simple_data
            data = data(:, :, 1:samples_read);
        else
            data = data(:, :, :, 1:samples_read);
        end
    end

    % Reads one block of timestamp, returns false if early end of data
    % encountered.  Normal case when timestamps are interleaved with the data
    % stream.
    function ok = read_timestamp_block()
        ok = true;
        try
            timestamp_buf = sc.read_bytes(ts_buf_size, true);
        catch me
            if ~strcmp(me.identifier, 'fa_load:read_bytes')
                rethrow(me)
            end

            % If we suffer from FA archiver buffer underrun it will be the
            % timestamp buffer that fails to be read.  If this occurs,
            % truncate sample_count and break -- we'll return what we have
            % in hand.
            ok = false;
            return
        end
        timestamps(ts_read + 1) = timestamp_buf.getLong();
        durations (ts_read + 1) = timestamp_buf.getInt();
        if save_id0
            id_zeros(ts_read + 1) = timestamp_buf.getInt();
        end
        ts_read = ts_read + 1;
    end


    % Reads timestamp block at end of data.  Special case for DD data when
    % interleaved timestamps slow us down too much.
    function read_timestamp_at_end()
        % Timestamps at end are sent as a count followed by all the timestamps
        % together followed by all the durations together.
        ts_read = sc.read_int_array(1);
        timestamps = sc.read_long_array(ts_read);
        durations  = sc.read_int_array(ts_read);
        if save_id0
            id_zeros = sc.read_int_array(ts_read);
        end
    end


    % Reads data and converts to target format
    function read_data_block()
        % Work out how many samples in the next block
        block_count = read_block_size - block_offset;
        if samples_read + block_count > sample_count
            block_count = sample_count - samples_read;
        end

        % Read the data and convert to matlab format.
        int_buf = sc.read_int_array(2 * field_count * id_count * block_count);
        if simple_data
            data(:, :, samples_read + 1:samples_read + block_count) = ...
                reshape(int_buf, 2, id_count, block_count);
        else
            data(:, :, :, samples_read + 1:samples_read + block_count) = ...
                reshape(int_buf, 2, 4, id_count, block_count);
        end

        block_offset = 0;
        samples_read = samples_read + block_count;
    end


    [field_count, block_offset, read_block_size, ...
     data, timestamps, durations, id_zeros] = prepare_data( ...
        sample_count, id_count, simple_data, initial_offset, block_size, ...
        ts_at_end, save_id0);
    if save_id0; ts_buf_size = 16; else ts_buf_size = 12; end

    % If possible create the wait bar
    if ~ts_at_end
        wh = progress_bar('Fetching data', show_bar);
    end

    % Read the requested data block by block
    samples_read = 0;
    ts_read = 0;
    while samples_read < sample_count
        if ~ts_at_end
            % Advance the progress bar and read the next timestamp block.  If
            % either of these fails then truncate the data and we're done.
            if ~wh.advance(samples_read / sample_count)  ||  ...
               ~read_timestamp_block()
                truncate_data();
                break
            end
        end

        read_data_block();
    end

    if ts_at_end
        read_timestamp_at_end();
    end
end


% Prepares final result.
function d = format_results( ...
        decimation, frequency, request_mask, perm, ...
        data, timestamps, durations, id_zeros, ...
        tz_offset, save_id0, simple_data, sample_count, ...
        block_size, initial_offset)

    % Prepare the result data structure
    d = struct();
    d.decimation = decimation;
    d.f_s = frequency;

    [d.day, d.timestamp, d.t] = process_timestamps( ...
        timestamps, durations, ...
        tz_offset, sample_count, block_size, initial_offset);
    if save_id0
        d.id0 = process_id0( ...
            id_zeros, sample_count, block_size, initial_offset, decimation);
    end

    % Restore the originally requested permutation if necessary.
    if any(diff(perm) ~= 1)
        d.ids = request_mask(perm);
        if simple_data
            d.data = data(:, perm, :);
        else
            d.data = data(:, :, perm, :);
        end
    else
        d.ids = request_mask;
        d.data = data;
    end
end


% Formats request mask into a format suitable for sending to the server.
function result = format_mask(mask, max_id)
    % Validate request
    if numel(mask) == 0
        error('Empty list of ids');
    end
    if mask(1) < 0 || max_id <= mask(end)
        error('Invalid range of ids');
    end

    % Assemble array of ints from ids and send as raw mask array
    mask_array = zeros(1, max_id / 32);
    for id = uint32(mask)
        ix = idivide(id, uint32(32)) + 1;
        mask_array(ix) = bitor(mask_array(ix), bitshift(1, mod(id, 32)));
    end
    result = ['R' sprintf('%08X', mask_array(end:-1:1))];
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
function [day, start_time, ts] = process_timestamps( ...
        timestamps, durations, ...
        tz_offset, sample_count, block_size, initial_offset)

    us_per_day = 1e6 * 3600 * 24;       % Scaling from archiver to Matlab time
    unix_epoch = 719529;                % 1970-01-01 in Matlab time

    % At this point we have to tread a little carefully.  The timestamps are in
    % microseconds in Unix epoch; as soon as we scale to Matlab time we will
    % lose precision -- there aren't enough bits in a double to represent
    % current time in Matlab format to microsecond precision, which is why ts is
    % returned as an offset relative to the starting day.
    %   This means we need to find the timestamp of the first point ... which
    % turns out to be not so straightforward because of initial_offset.  Also,
    % we need to take care about when we're working with integers and when
    % we're not.

    try int64(1)+int64(1); catch
        % On older versions of matlab the next assignment will fail because
        % arithmetic isn't implemented for 64-bit integers!  This conversion can
        % lose a bit of precision, but to be honest the impact is pretty
        % marginal!
        timestamps = double(timestamps);
    end

    % First compute the start time in archiver units, and from this we can
    % compute the day and start time.
    start_time_us = ...
        timestamps(1) + (initial_offset * durations(1)) / block_size;

    raw_start_time = double(start_time_us) / us_per_day + tz_offset;
    raw_day = floor(raw_start_time);

    start_time = raw_start_time + unix_epoch;
    day = raw_day + unix_epoch;

    % Convert timestamps in archiver format into relative timestamps in Matlab
    % format.
    day_us = raw_day * us_per_day;
    timestamps = double(timestamps - day_us) / us_per_day + tz_offset;

    % Convert durations into corrections for timestamps.
    durations = durations * (0 : block_size - 1) / block_size / us_per_day;
    ts = repmat(timestamps, 1, block_size) + durations;
    ts = reshape(ts', [], 1);
    ts = ts(initial_offset + 1:initial_offset + sample_count);
end


% Computes id0 array from captured information.
function id0 = process_id0( ...
        id_zeros, sample_count, block_size, initial_offset, decimation)
    id_zeros = int32(id_zeros);
    offsets = int32(decimation * (0 : block_size - 1));
    id0 = repmat(id_zeros, 1, block_size) + repmat(offsets, size(id_zeros), 1);
    id0 = reshape(id0', [], 1);
    id0 = id0(initial_offset + 1:initial_offset + sample_count);
end
