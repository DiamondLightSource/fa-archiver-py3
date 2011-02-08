function fa_zoomer(server)
% fa_zoomer([server])
%
% Investigate and zoom into archive data for the last day.

% h is used to store all persistent state that is not part of the generated
% data.
h = {};

if nargin == 0
    h.server = 'fa-archiver.cs.diamond.ac.uk';
elseif server == 'booster'
    h.server = '172.23.234.70';
else
    h.server = server;
end

% Create figure with the standard toolbar but no menubar
fig = figure('MenuBar', 'none', 'Toolbar', 'figure');

% Create the controls.
h.bpm_list = uicontrol('String', '4', 'Position', [10 10 60 20], ...
    'Style', 'edit');
uicontrol('String', 'Last day', 'Position', [80 10 60 20], ...
    'Callback', @last_day_callback);
uicontrol('String', 'Zoom',     'Position', [150 10 60 20], ...
    'Callback', @zoom_in_callback);
uicontrol('String', 'Spectrogram', 'Position', [220 10 100 20], ...
    'Callback', @spectrogram_callback);
h.message = uicontrol('Position', [330 10 150 20], 'Style', 'text');

% Hang onto the controls we need to reference later
guidata(fig, h);


function last_day_callback(fig, event)
h = guidata(fig);
global data;

pvs = str2num(get(h.bpm_list, 'String'));

busy;
data = fa_load([now - 1.5 now], pvs, 'D', h.server);
plotfa(data);
describe;


function zoom_in_callback(fig, event)
h = guidata(fig);
global data;

maxdata = 1e7;
points = diff(xlim) * 24 * 3600 * 10072 * length(data.ids);

type = 'F';
if points > maxdata
    type = 'd';
    points = points / 128;
    if points > maxdata
        type = 'D';
    end
end

busy;
data = fa_load(xlim+data.day, data.ids, type, h.server);
plotfa(data);
describe;


function spectrogram_callback(fig, event)
global data;
len=1024;

busy;
for n = 1:2
    subplot(2, 1, n)
    if length(size(data.data))==3
        e = squeeze(data.data(n, 1, :));
        cols = floor(length(e)/len);
        sg = log10(abs(fft(reshape(e(1:(len*cols)), len, cols))));
        imagesc(data.t, [0 data.f_s/5], sg(1:(round(len/5)), :));
        set(gca, 'Ydir', 'normal');
        caxis([2 6])
    end
    datetick('keeplimits')
    title(datestr(data.day))
end
describe;


function plotfa(d)
axes = {'X'; 'Y'};
for n = 1:2
    subplot(2, 1, n)
    if length(size(d.data))==4
        plot(d.t, 1e-3 * squeeze(d.data(n, 2, :, :))); hold on
        plot(d.t, 1e-3 * squeeze(d.data(n, 3, :, :))); hold off
    else
        plot(d.t, 1e-3 * squeeze(d.data(n, :, :)))
    end

    ylim([-100 100]);
    datetick('keeplimits')
    title([datestr(d.day) ' ' axes{n}])
end


function message(msg)
h = guidata(gcf);
set(h.message, 'String', msg);

function busy
message('Busy');
drawnow;

function describe
global data;
message(sprintf('%d* %d/%d', ...
    length(data.ids), length(data.data), data.decimation))
