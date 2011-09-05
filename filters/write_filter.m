% write_filter(filename, filter)
%
% Writes filter structure computed with make_filter to given file in format
% suitable for use as an fa-archiver decimation configuration file.

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
function write_filter(filename, filter)

N = 5;
len = length(filter.fir);

file = fopen(filename, 'w');
fprintf(file, '# CIC + FIR decimation filter\n');
fprintf(file, '# Transition: %g%% to %g%% of decimation, %d point FIR\n', ...
    100 * filter.pass, 100 * filter.stop, len);
fprintf(file, 'decimation_factor = %d\n', filter.cic_decimation);
fprintf(file, 'filter_decimation = %d\n', filter.fir_decimation);
fprintf(file, 'comb_orders =');
fprintf(file, ' %d', filter.comb);
fprintf(file, '\n');
fprintf(file, 'compensation_filter = \\\n');
rows = floor((len + N - 1) / N);
for r = 1 : rows - 1
    fprintf(file, '  %e', filter.fir(1, N * (r - 1) + 1 : N * r));
    fprintf(file, '  \\\n');
end
fprintf(file, '  %e', filter.fir(1, N * (rows - 1) + 1 : end));
fprintf(file, '\n');

fclose(file);
