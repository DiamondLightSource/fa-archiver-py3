% Returns the response of the given cic filter
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
