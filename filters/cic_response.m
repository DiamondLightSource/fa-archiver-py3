% Returns the response of the given cic filter
function response = cic_response(comb, decimation, f)

epsilon = 1e-6;
fz = abs(f) < epsilon;
fn0 = pi * f(find(~fz));

resp = ones(1, length(fn0));
r0 = 1;
for c = comb
    r = decimation * c;
    resp = resp .* sin(r * fn0) ./ sin(fn0);
    r0 = r0 * r;
end
response = zeros(1, length(f));
response(find(fz)) = 1;
response(find(~fz)) = resp / r0;
