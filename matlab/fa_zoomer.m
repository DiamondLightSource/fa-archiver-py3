function fa_zoomer(server)
% fa_zoomer([server])
%
% Investigate and zoom into archive data for the last day.

% h is used to store all persistent state that is not part of the generated
% data.
h = {};

if nargin == 0
    h.server = 'fa-archiver.cs.diamond.ac.uk';
elseif strcmp(server, 'booster')
    h.server = '172.23.234.70';
else
    h.server = server;
end

% Create figure with the standard toolbar but no menubar
fig = figure('MenuBar', 'none', 'Toolbar', 'figure', ...
    'Position', [0 0 900 600]);
global data;
data = {};

% Create the controls.
global h_pos;
h_pos = 10;
h.bpm_list = control('edit', '4', 60, 'List of BPM FA ids');
control('pushbutton', 'Full', 40, 'View entire archive history', ...
    'Callback', @full_archive_callback);
control('pushbutton', '24h', 40, 'View last 24 hours', ...
    'Callback', @last_day_callback);
control('pushbutton', 'Zoom', 60, 'Update zoomed area from archive', ...
    'Callback', @zoom_in_callback);
control('pushbutton', 'Spectrogram', 100, 'Show as spectrogram', ...
    'Callback', @spectrogram_callback);
h.message = control('text', '', 150, ...
    'Error message or [bpm count] samples/decimation');
h.maxpts = control('edit', num2str(1e6), 80, ...
    'Maximum number of sample points');
h.ylim = control('checkbox', 'Zoomed', 80, ...
    'Limit vertical scale to +-100um', 'Value', 1);
clear global h_pos;

% Hang onto the controls we need to reference later
guidata(fig, h);

last_day_callback(fig, 0);


% Places control with specified style, value, width  and tooltip.
function result = control(style, value, width, tooltip, varargin)
global h_pos;
position = [h_pos 10 width 20];
h_pos = h_pos + width + 5;
result = uicontrol( ...
    'Style', style, 'String', value, 'Position', position, ...
    'TooltipString', tooltip, varargin{:});


function full_archive_callback(fig, event)
load_data(fig, [get_archive_start(fig) now], 'D');


% Loads data for the last 24 hours
function last_day_callback(fig, event)
load_data(fig, [now-1 now], 'D');


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

load_data(fig, xlim + data.day, type);


% Loads the requested range of data.
function load_data(fig, range, type)
h = guidata(fig);
global data;

pvs = str2num(get(h.bpm_list, 'String'));

busy;
data = fa_load(range, pvs, type, h.server);
plotfa(data);
describe;


function spectrogram_callback(fig, event)
global data;
len=1024;

if length(size(data.data)) == 3
    busy;
    for n = 1:2
    subplot(2, 1, n)
        e = squeeze(data.data(n, 1, :));
        cols = floor(length(e)/len);
        sg = log10(abs(fft(reshape(e(1:(len*cols)), len, cols))));
        imagesc(data.t, [0 data.f_s/5], sg(1:(round(len/5)), :));

        set(gca, 'Ydir', 'normal');
        caxis([2 6])
        label_axis(n)
    end
    describe;
else
    message('Decimated data');
end


function plotfa(d)
h = guidata(gcf);
for n = 1:2
    subplot(2, 1, n)
    if length(size(d.data)) == 4
        plot(d.t, 1e-3 * squeeze(d.data(n, 2, :, :))); hold on
        plot(d.t, 1e-3 * squeeze(d.data(n, 3, :, :))); hold off
    else
        plot(d.t, 1e-3 * squeeze(d.data(n, :, :)))
    end

    xlim([d.t(1) d.t(end)]);
    if get(h.ylim, 'Value'); ylim([-100 100]); end
    label_axis(n)
end


function label_axis(n)
axes = {'X'; 'Y'};
global data;
if diff(data.t([1 end])) <= 2/(24*3600)
    title([datestr(data.timestamp) ' ' axes{n}])
    set(gca, 'XTickLabel', num2str( ...
        get(gca,'XTick').'*24*3600-60*floor(data.t(1)*24*60),'%.4f'))
else
    title([datestr(data.day) ' ' axes{n}])
    datetick('keeplimits')
end

function message(msg)
h = guidata(gcf);
set(h.message, 'String', msg);

function busy
message('Busy');
drawnow;

% Prints description of currently plotted data
function describe
global data;
message(sprintf('[%d] %d/%d', ...
    length(data.ids), length(data.data), data.decimation))


% Interrogates start time from archiver.
function start = get_archive_start(fig)
h = guidata(gcf);
[r, start_secs] = system(['echo CT | nc ' h.server ' 8888']);
if r ~= 0
    error(start_secs)
end
% Convert Unix epoch time in seconds into Matlab epoch in days.
start = 719529 + str2num(start_secs) / 3600 / 24;
