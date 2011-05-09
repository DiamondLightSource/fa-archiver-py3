% fa_load
%
% Grab bpm data from the fa archiver for the last 36 hours or so.
%
% d = fa_load(tse, mask [, type [,server]])
%
% Input:
%   tse = [start_time end_time]
%   mask = vector containing bmps [1 2 3], or [78 79] etc.
%   type = 'F' for full data
%          'd' for 10072/64 decimated
%          'D' for 10072/16384 decimated
%
% Output:
%   d = object containing all of the data
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


    % Create temporary file to capture into
    [r, famat] = system('mktemp');
    if r ~= 0; error('mktemp failed'); end
    famat = famat(1:end-1);     % Remove \n from returned filename

    % Use fa-capture utility to read required data into temporary file
    maskstr = sprintf('%d,', mask);
    [r, o] = system([ ...
        fa_capture ' -ka -s' format_time(tse(1)) '~' format_time(tse(2)) ...
        ' -o' famat ' -f' type ' ' server ' ' maskstr(1:end-1)]);
    if r ~= 0
        delete(famat);
        error(o)
    end

    % Load the read data into memory
    d = load('-mat', famat);
    delete(famat);

    % Compute the timestamps
    g = [d.gapix length(d.data)] + 1;
    for n = 1:(length(g)-1);
        d.t(g(n):g(n+1)-1) = ...
            d.gaptimes(n) + (0:diff(g([n n+1]))-1)/d.f_s/3600/24;
    end
    d.day = floor(d.t(1));
    d.t = d.t - d.day;
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
