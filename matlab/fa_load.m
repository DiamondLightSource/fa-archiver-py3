% loadfa
%   
% Guenther Rehm
% November 2010
%
% Grab bpm data from the fa archiver for the last 36 hours or so.
%
% d = loadfa(tse, mask, type)
%
% Input:
%   tse = [start_time end_time]
%   mask = vector containing bmps [1 2 3], or [78 79] etc.
%   type = 'F' for full data
%          'd' for 10072/64 decimated
%          'D' for 10072/128^2 decimated
%
% Output:
%   d = object containing all of the data
function d = loadfa(tse, mask, type, server)
if nargin<3
    type = 'F';
end
if nargin < 4
    server = 'fa-archiver.cs.diamond.ac.uk';
end

switch type
    case 'F'
        rate = 10072;
    case 'd'
        rate = 10072/64;
    case 'D'
        rate = 10072/128^2;
end
[r, famat] = system('mktemp');
famat = famat(1:end-1);
if r ~= 0; error('no mktmp?'); end
start = tse(1);
n = floor(diff(tse)*24*3600*rate);
maskstr = sprintf('%d,', mask);
[r, o] = system([ ...
    'fa-capture -ka -s' datestr(start, 'yyyy-mm-ddTHH:MM:SS') ...
    ' -o' famat ' -f' type ' -S' server ' ' maskstr(1:end-1) ' ' num2str(n)]);
if r ~= 0
    delete(famat);
    error(o)
end
d = load('-mat', famat);
g = [d.gapix length(d.data)]+1;
for n = 1:(length(g)-1);
    d.t(g(n):(g(n+1)-1)) = d.gaptimes(n)+(0:diff(g([n n+1]))-1)/d.f_s/3600/24;
end
d.day = floor(d.t(1));
d.t = d.t-d.day;
delete(famat);
