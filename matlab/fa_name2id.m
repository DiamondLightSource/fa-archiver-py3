% function ids = fa_name2id(names)
% returns FA ids for names (either one string or a cell array of strings)
function ids = fa_name2id(names)
    fa_ids_file = '/home/ops/diagnostics/concentrator/fa-ids.sr';
    [allids, allnames] = ...
        textread(fa_ids_file, '%n %s', 'commentstyle', 'shell');

    % Unfortunately, a single string will be a 'char array' while multiple
    % strings will come as a cell array, so to make sure we can live with both,
    % we check and make it a cell array if it is not already.
    if iscell(names)
        names_c = names;
    else
        names_c{1} = names;
    end

    for n = 1:length(names_c)
        f = find(strcmp(names_c(n), allnames));
        if length(f) > 1
            error(['More than one matching FA ID for "' names_c{n} '"'])
        elseif isempty(f)
            error(['Could not find FA ID for "' names_c{n} '"'])
        else
            ids(n) = allids(f);
        end
    end
end
