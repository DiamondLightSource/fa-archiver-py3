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
