% response = cic_response(com, decimation, f)
%
% Returns the response of the given cic filter
% Helper function used by make_filter.m

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
function response = cic_response(comb, decimation, f)

epsilon = 1e-6;
fz = abs(sin(pi * f)) < epsilon;
fn0 = pi * f(find(~fz));

resp = ones(1, length(fn0));
r0 = 1;
for order = 1:length(comb)
    r = decimation * order;
    resp = resp .* (sin(r * fn0) ./ sin(fn0)).^comb(order);
    r0 = r0 * r.^comb(order);
end
response = zeros(1, length(f));
response(find(fz)) = 1;
response(find(~fz)) = resp / r0;
