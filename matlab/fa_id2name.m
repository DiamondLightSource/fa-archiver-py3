% function names = fa_id2name(ids)
% returns names for a vector of FA ids
function names = fa_id2name(ids)
    fa_ids_file = '/home/ops/diagnostics/concentrator/fa-ids.sr';
    [allids, allnames] = ...
        textread(fa_ids_file, '%n %s', 'commentstyle', 'shell');

    allnames_i(allids+1) = allnames;
    names = allnames(ids+1);
    % An annoying little hack: handling cells for single values causes trouble.
    if length(names) == 1
        names = cell2mat(names);
    end
end
