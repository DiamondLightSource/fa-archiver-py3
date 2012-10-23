% fa_zoomer([server])
%
% Investigate and zoom into archive data for the last day.

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
function fa_zoomer(server)
    % h is used to store all persistent state that is not part of the generated
    % data.
    h = {};

    if nargin == 0
        h.server = 'fa-archiver.diamond.ac.uk';
    elseif strcmp(server, 'booster')
        h.server = 'fa-archiver.diamond.ac.uk:8889';
    else
        h.server = server;
    end

    % Create figure with the standard toolbar but no menubar
    fig = figure('MenuBar', 'none', 'Toolbar', 'figure', ...
        'Position', [0 0 900 600]);
    global data;
    data = {};

    % Create the controls.
    global h_pos v_pos;
    h_pos = 10; v_pos = 10;
    h.bpm_list = control('edit', '4', 60, 'List of BPM FA ids');
    control('pushbutton', 'Back', 40, 'Return to previous zoom', ...
        'Callback', protect(@back_callback));
    control('pushbutton', 'Full', 40, 'View entire archive history', ...
        'Callback', protect(@full_archive_callback));
    control('pushbutton', '24h', 40, 'View last 24 hours', ...
        'Callback', protect(@last_day_callback));
    control('pushbutton', 'Zoom', 60, 'Update zoomed area from archive', ...
        'Callback', protect(@zoom_in_callback));
    control('pushbutton', 'Spectrogram', 100, 'Show as spectrogram', ...
        'Callback', protect(@spectrogram_callback));
    h.message = control('text', '', 150, ...
        'Error message or [bpm count] samples/decimation');
    h.maxpts = control('edit', num2str(1e6), 80, ...
        'Maximum number of sample points');
    h.ylim = control('checkbox', 'Zoomed', 70, ...
        'Limit vertical scale to +-100um', 'Value', 1, ...
        'Callback', protect(@reload_plot));
    h.data_type = control('popup', {'min/max', 'std', 'mean'}, 90, ...
        'Choose decimated data type to display', 'Value', 1, ...
        'Callback', protect(@reload_plot));
    control('pushbutton', 'Save', 40, 'Save data to file', ...
        'Callback', protect(@save_data));

    % Some extra controls on the line above
    h_pos = 10; v_pos = v_pos + 30;
    control('popup', fa_getids(), 150, 'Valid BPM names', ...
        'Value', 4, 'Callback', protect(@set_bpm_list));

    clear global h_pos v_pos;
    h.history = cell(0, 2);

    % Hang onto the controls we need to reference later
    guidata(fig, h);

    last_day_callback(fig, 0);
end


% Places control with specified style, value, width  and tooltip.
function result = control(style, value, width, tooltip, varargin)
    global h_pos v_pos;
    position = [h_pos v_pos width 20];
    h_pos = h_pos + width + 5;
    result = uicontrol( ...
        'Style', style, 'String', value, 'Position', position, ...
        'TooltipString', tooltip, varargin{:});
end


function prot = protect(func)
    function protected(fig, event)
        try
            func(fig, event)
        catch
            message('Error: see console')
            rethrow(lasterror)
        end
    end
    prot = @protected;
end


function set_bpm_list(fig, event)
    h = guidata(fig);
    names = get(fig, 'String');
    index = get(fig, 'Value');
    name = names{index};
    id = fa_name2id(name);
    set(h.bpm_list, 'String', id)
end


function back_callback(fig, event)
    h = guidata(fig);
    if size(h.history, 1) > 1
        h.history = h.history(1:end-1, :);
        range = h.history{end, 1};
        type  = h.history{end, 2};
        guidata(fig, h)
        load_data(fig, range, type, false);
    end
end


function full_archive_callback(fig, event)
    load_data(fig, [now-2000 now], 'D', true);    % Go back as far as possible!
end


% Loads data for the last 24 hours
function last_day_callback(fig, event)
    load_data(fig, [now-1 now], 'D', true);
end


% Loads data enclosed by the current zoom selection, returned by xlim.
function zoom_in_callback(fig, event)
    h = guidata(fig);
    global data;

    maxdata = str2num(get(h.maxpts,   'String'));
    pvs     = str2num(get(h.bpm_list, 'String'));
    points = diff(xlim) * 24 * 3600 * 10072 * length(pvs);

    type = 'F';
    if points > maxdata
        type = 'd';
        points = points / 64;
        if points > maxdata
            type = 'D';
        end
    end

    load_data(fig, xlim + data.day, type, true);
end


function reload_plot(fig, event)
    h = guidata(fig);
    global data;
    plotfa(h, data);
end


% Loads the requested range of data.
function load_data(fig, range, type, save)
    h = guidata(fig);
    if save
        h.history(end+1, :) = {range, type};
        guidata(fig, h)
    end
    global data;

    pvs = str2num(get(h.bpm_list, 'String'));

    busy;
    data = fa_load(range, pvs, type, h.server);
    plotfa(h, data);
    describe;
end


% Saves data to file
function save_data(fig, event)
    global data;
    [file, path] = uiputfile('*.mat', 'Save archive data');
    if ~isequal(file, 0) & ~isequal(path, 0)
        save(fullfile(path, file), '-struct', 'data');
    end
end


% Hann window for improved spectral resolution.
function a = hannwin(n, m)
    t = linspace(0, 2*pi, n+1);
    t = t(1:n);
    a = 0.5 * (1 - cos(t)).';
    if nargin == 2
        a = repmat(a, 1, m);
    end
end


% Plot spectrogram
function spectrogram_callback(fig, event)
    global data;
    len = 1024;

    scale = 1e-3 * sqrt(2 / (len * data.f_s));
    if length(size(data.data)) == 3
        busy;
        for n = 1:2
        subplot(2, 1, n)
            e = squeeze(data.data(n, 1, :));
            cols = floor(length(e)/len);
            sg = log10(scale * abs(fft( ...
                reshape(e(1:(len*cols)), len, cols) .* hannwin(len, cols))));
            imagesc(data.t, [0 data.f_s/5], sg(1:(round(len/5)), :));

            set(gca, 'Ydir', 'normal');
            colorbar
            label_axis(n, 'Hz')
        end
        describe;
    else
        message('Decimated data');
    end
end


function plotfa(h, d)
    data_type = get(h.data_type, 'Value');
    if get(h.ylim, 'Value')
        if length(size(d.data)) == 4
            switch data_type
                case 1;     set_ylim = [-100 100];
                case 2;     set_ylim = [0 10];
                case 3;     set_ylim = [-10 10];
            end
        else
            set_ylim = [-100 100];
        end
    else
        set_ylim = [];
    end

    if isunix; mum = 'µm'; else mum = '\mu m'; end  % UTF-8 mu not for Windows!
    for n = 1:2
        subplot(2, 1, n)
        if length(size(d.data)) == 4
            switch data_type
                case 1
                    plot(d.t, 1e-3 * squeeze(d.data(n, 2, :, :))); hold on
                    plot(d.t, 1e-3 * squeeze(d.data(n, 3, :, :))); hold off
                case 2
                    plot(d.t, 1e-3 * squeeze(d.data(n, 4, :, :)));
                case 3
                    plot(d.t, 1e-3 * squeeze(d.data(n, 1, :, :)));
            end
        else
            plot(d.t, 1e-3 * squeeze(d.data(n, :, :)))
        end

        xlim([d.t(1) d.t(end)]);
        if length(set_ylim) > 0; ylim(set_ylim); end
        label_axis(n, mum)
    end
end


function label_axis(n, yname)
    axes = {'X'; 'Y'};
    global data;
    ylabel(gca, yname);
    if diff(data.t([1 end])) <= 2/(24*3600)
        title([datestr(data.timestamp) ' ' axes{n}])
        set(gca, 'XTickLabel', num2str( ...
            get(gca,'XTick').'*24*3600-60*floor(data.t(1)*24*60),'%.4f'))
    else
        title([datestr(data.day) ' ' axes{n}])
        datetick('keeplimits')
    end
end

function message(msg)
    h = guidata(gcf);
    set(h.message, 'String', msg);
end

function busy
    message('Busy');
    drawnow;
end

% Prints description of currently plotted data
function describe
    global data;
    message(sprintf('[%d] %d/%d', ...
        length(data.ids), length(data.data), data.decimation))
end
