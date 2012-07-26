% d = fa_capture(tse, mask [, type [,server]])
%
% Grab bpm data from the FA archiver.  This is a this wrapper around the
% fa-capture application.  For normal use fa_load should be used instead.
%
% Input:
%   tse = [start_time end_time]
%   mask = vector containing FA ids [1 2 3], or [78 79] etc.
%   type = 'F' for full data
%          'd' for 10072/64 decimated
%          'D' for 10072/16384 decimated
%          'C' for continuous data, in which case tse must be a single number
%              specifying the number of samples
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
%   d.t             Timestamp array roughly timestamping each sample point
%   d.day           Matlab number of day containing first sample
%
% FA ids are returned in the same order that they were requested, including
% any duplicates.

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
function d = fa_load(tse, mask, type, server)
    fa_capture = 'fa-capture';
    if nargin < 3
        type = 'F';
    end
    if nargin < 4
        server = '';
    else
        server = ['-S' server];
    end

    % Compute unique sorted list from id mask and remember the permuation.
    [request_mask, dummy, perm] = unique(mask);

    % Create temporary file to capture into
    [r, famat] = system('mktemp');
    if r ~= 0; error('mktemp failed'); end
    famat = famat(1:end-1);     % Remove \n from returned filename

    % Use fa-capture utility to read required data into temporary file
    maskstr = sprintf('%d,', request_mask);
    if type == 'C'
        args = '-C';
        capture_count = sprintf(' %d', tse);
    else
        args = ['-s' format_time(tse(1)) '~' format_time(tse(2)) ' -f' type];
        capture_count = '';
    end
    [r, o] = system([ ...
        fa_capture ' -kad ' args ...
        ' -o' famat ' ' server ' ' maskstr(1:end-1) capture_count]);
    if r ~= 0
        delete(famat);
        error(o)
    end

    % Load the read data into memory
    d = load('-mat', famat);
    delete(famat);

    % Restore the originally requested permutation if necessary.
    if any(diff(perm) ~= 1)
        d.ids = d.ids(perm);
        if type == 'F' || type == 'C'
            d.data = d.data(:, perm, :);
        else
            d.data = d.data(:, :, perm, :);
        end
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
