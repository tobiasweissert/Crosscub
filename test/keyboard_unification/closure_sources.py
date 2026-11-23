"""Exact-source keyboard boundaries; never a second layout implementation."""
import hashlib,json,re

def function(source, token):
    start=source.index(token); brace=source.index('{',start); depth=1;i=brace+1
    # Strip comments/strings for brace counting while preserving byte offsets.
    masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),source,flags=re.S)
    while depth:
        depth += (masked[i]=='{')-(masked[i]=='}'); i+=1
    return source[start:i]

def generate(root,out):
    bindings=[]
    path='src/activities/reader/EpubReaderTouchMenuActivity.cpp';source=(root/path).read_text()
    methods=['buildDrawer','buildTabBar','buildPaneHeader','buildPercentPane','buildStablePagePane','buildDrawerKeypad','resetKeypadEntry','appendKeypadDigit','appendKeypadDecimalPoint','backspaceKeypadEntry','syncKeypadValue','onKeypadKeyEvent','onKeypadBackspaceEvent','completePercentSelection','completeStablePageSelection']
    result=''
    for name in methods:
        exact=function(source,'void EpubReaderTouchMenuActivity::'+name+'(')
        bindings.append(dict(source=path,method=name,sha256=hashlib.sha256(exact.encode()).hexdigest(),start_line=source[:source.index(exact)].count('\n')+1))
        result+=exact.replace('EpubReaderTouchMenuActivity','DrawerBoundary')+'\n'
    helpers=source[source.index('constexpr int16_t TAB_BAR_HEIGHT'):source.index('StrId readerOrientationLabel')]
    (out/'drawer_constants.inc').write_text(helpers)
    for token in ['int16_t EpubReaderTouchMenuActivity::drawerHeight(', 'const char* EpubReaderTouchMenuActivity::paneTitle(']:
        exact=function(source,token)
        bindings.append(dict(source=path,token=token,sha256=hashlib.sha256(exact.encode()).hexdigest()))
        result+=exact.replace('EpubReaderTouchMenuActivity','DrawerBoundary')+'\n'
    (out/'drawer_extracted.inc').write_text(result)
    def extract(path,token,new_name=None):
        source=(root/path).read_text();exact=function(source,token)
        bindings.append(dict(source=path,token=token,sha256=hashlib.sha256(exact.encode()).hexdigest(),start_line=source[:source.index(exact)].count('\n')+1))
        if new_name:return 'void '+('WifiBoundary' if new_name.startswith('wifi') else 'CallerBoundary')+'::'+new_name+'() '+exact[exact.index('{'):]+ '\n'
        return exact+'\n'
    result=''
    for path,token,name in [
      ('src/activities/CatalogActivity.cpp','void CatalogActivity::launchSearch(', 'catalog'),
      ('src/activities/library/LibraryActivity.cpp','void LibraryActivity::openSearch(', 'library'),
      ('src/activities/network/WifiSelectionActivity.cpp','void WifiSelectionActivity::promptPasswordEntry(', 'wifiPassword'),
      ('src/activities/network/WifiSelectionActivity.cpp','void WifiSelectionActivity::promptHiddenSsid(', 'wifiSsid'),
      ('src/activities/settings/KOReaderSettingsActivity.cpp','if (selectedIndex == 0)', 'koUsername'),
      ('src/activities/settings/KOReaderSettingsActivity.cpp','if (selectedIndex == 1)', 'koPassword'),
      ('src/activities/settings/KOReaderSettingsActivity.cpp','if (selectedIndex == 2)', 'koUrl'),
      ('src/activities/settings/KOReaderSettingsActivity.cpp','if (selectedIndex == 9 || selectedIndex == 10)', 'koHeader'),
      ('src/activities/settings/OpdsSettingsActivity.cpp','if (nav.selected == 0)', 'opdsName'),
      ('src/activities/settings/OpdsSettingsActivity.cpp','if (nav.selected == 1)', 'opdsUrl'),
      ('src/activities/settings/OpdsSettingsActivity.cpp','if (nav.selected == 2)', 'opdsUsername'),
      ('src/activities/settings/OpdsSettingsActivity.cpp','if (nav.selected == 3)', 'opdsPassword'),
      ('src/activities/settings/OpdsSettingsActivity.cpp','if (nav.selected == CUSTOM_HEADER_1_INDEX || nav.selected == CUSTOM_HEADER_2_INDEX)', 'opdsHeader'),
      ('src/activities/settings/OpdsServerListActivity.cpp','if (nav.selected == serverCount + 1)', 'opdsFolder'),
    ]:result+=extract(path,token,name)
    # Keep complete initiating methods including the captured result closure.
    for path,token,old,new in [
      ('src/activities/home/FileBrowserActivity.cpp','void FileBrowserActivity::startRenameFile(','FileBrowserActivity','CallerBoundary'),
      ('src/activities/reader/EpubReaderBookmarksActivity.cpp','void EpubReaderBookmarksActivity::startRename(','EpubReaderBookmarksActivity','CallerBoundary'),
      ('src/activities/settings/SettingsActivity.cpp','void SettingsActivity::openStringEditor(','SettingsActivity','CallerBoundary'),
      ('src/activities/settings/SettingsActivity.cpp','void SettingsActivity::openFrontlightScheduleTimePicker(','SettingsActivity','CallerBoundary')
    ]:result+=extract(path,token).replace(old,new).replace('BookmarkFile::save(', 'neutralBookmarkSave(')
    # Production normalization and validation, not parallel fixture algorithms.
    result=extract('src/activities/home/FileBrowserActivity.cpp','std::string getFileExtension(const std::string& filename) {')+extract('src/activities/settings/OpdsServerListActivity.cpp','std::string normalizeFolder(')+extract('src/activities/settings/SettingsActivity.cpp','std::string trimAsciiSpaces(')+result
    lcp=extract('src/activities/browser/OpdsBookBrowserActivity.cpp','void OpdsBookBrowserActivity::promptLcpPassphrase(').replace('OpdsBookBrowserActivity::','LcpBoundary::')
    lcp+=extract('src/activities/browser/OpdsBookBrowserActivity.cpp','OpdsBookBrowserActivity::LcpUnlock OpdsBookBrowserActivity::requestLcpUnlock(').replace('OpdsBookBrowserActivity::','LcpBoundary::')
    lcp=lcp.replace('lcpdevice::','boundaryLcpDevice::').replace('HttpDownloader::','UnreachedTransport::').replace('bookkey::','UnreachedBookKey::')
    enrollment=extract('src/network/LcpDevice.cpp','bool ensureEnrolled(').replace('HttpDownloader::','UnreachedTransport::').replace('base64::','UnreachedBase64::')
    (out/'lcp_extracted.inc').write_text('namespace boundaryLcpDevice {\n'+enrollment+'}\n'+lcp)
    match=re.search(r'constexpr size_t NAME_BUFFER_SIZE = [0-9]+;', (root/'src/activities/home/FileBrowserActivity.cpp').read_text())
    assert match
    name_size=match[0]
    (out/'caller_constants.inc').write_text(name_size+'\n')
    (out/'caller_extracted.inc').write_text(result)
    (out/'closure-extractions.json').write_text(json.dumps(bindings,indent=2)+'\n')
