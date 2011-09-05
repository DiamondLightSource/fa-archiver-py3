% result = make_filter( ...
%    comb, cic_decimation, fir_len, fir_decimation, pass, stop)
%
% Computes compensation filter for given CIC filter

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
function result = make_filter( ...
    comb, cic_decimation, len, fir_decimation, pass, stop)

% Compute target filter with given CIC parameters meeting the specified pass and
% stop bands for the requested FIR decimation factor.
filter = fit_inverse( ...
    comb, cic_decimation, len, pass / fir_decimation, stop / fir_decimation);
filter = filter / sum(filter);

M = 1024;
N = M * fir_decimation;
f = linspace(0, 0.5, N);
cic = cic_response(comb, cic_decimation, f / cic_decimation);
ff = fft([filter zeros(1, 2 * N - len - 1)]);
ff = ff(1:N);

response = cic_response( ...
    comb, cic_decimation, linspace(0, 0.5, N * cic_decimation));
response = fold(response, cic_decimation) .* repmat(ff, cic_decimation, 1);
response = fold(response, fir_decimation);
response = response';

% Create result structure with complete information
result = {};
result.pass = pass;
result.stop = stop;
result.comb = comb;
result.cic_decimation = cic_decimation;
result.fir_decimation = fir_decimation;
result.fir = filter;
result.response = response;


subplot(2, 2, 1)
plot(filter)
title(sprintf('Filter: length = %d', len))
xlim([1 len+1])

subplot(2, 2, 2)
plot(f, dB(cic), f, dB(ff), f, dB(cic .* ff))
title('Compensation filter response')
legend('CIC', 'Compensation', 'Final', 'location', 'southwest')
xlabel('CIC sample frequency')
ylabel('Attenuation in dB')
ylim([-160 20])

subplot(2, 2, 3)
plot(f * fir_decimation, abs(cic .* ff))
title('Passband response')
xlabel('Output sample frequency')
xlim([0 0.5])
ylim([0.9 1.1])

subplot(2, 2, 4)
plot(linspace(0, 0.5, M), dB(response))
title('Aliasing Response')
xlabel('Output sample frequency')
ylim([-160 20])



function filter = fit_inverse(comb, decimation, len, pass, stop)
passband = linspace(0, pass);
stopband = linspace(stop, 1);
freq = [passband stopband];
cic = cic_response(comb, decimation, 0.5 * passband / decimation);
target = [1 ./ cic  zeros(1, length(stopband))];
filter = fir2(len, freq, target)';


function folded = fold(wf, folds)
rows = size(wf, 1);
cols = size(wf, 2);
row_size = cols / folds;
folded = zeros(rows * folds, row_size);
for i = 1:rows
    for j = 1:folds
        rix = (i - 1) * folds + j;
        row = wf(i, (j - 1) * row_size + 1 : j * row_size);
        if mod(rix, 2) == 0
            row = row(:, end:-1:1);
        end
        folded(rix, :) = row;
    end
end


function result = dB(wf)
result = 20 * log10(abs(wf));
