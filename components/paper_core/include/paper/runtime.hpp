#pragma once
#include "document.hpp"
#include "ime.hpp"
#include "services.hpp"
#include "font_catalog.hpp"
#include "ui.hpp"
namespace paper {
    enum class Screen {
        Home,Library,Reading,Toc,Bookmarks,SearchResults,Settings,Setting,Input,Transfer,ReaderFonts,ReaderFontFamilies,ReaderFontValues,ReaderFontInbox,SettingsList,Maintenance,Network,Bluetooth
    };
    struct Hit {
        Rect box;
        std::string action,value;
        bool enabled=true;
    };
    struct DisplayJob {
        uint64_t revision=0;
        uint64_t inputEpoch=0;
        bool full=true,gray=false;
        Canvas frame;
        std::vector<Hit>hits;
    };
    class Runtime {
        ResourceGate gate_;
        Store store_;
        PackedFonts fonts_;
        ReaderFontCatalog fontCatalog_;
        ReaderStyle fontDraft_;
        std::vector<ReaderFontFamily>fontFamilies_;
        std::vector<FileEntry>fontInbox_;
        std::string fontField_,fontPreviewError_;
        std::string settingCategory_;
        std::string networkSsid_;
        std::vector<NetworkAccessPoint> networkAps_;
        NetworkState drawnNetwork_;
        BluetoothState drawnBluetooth_;
        std::vector<ui::Node> uiAudit_;
        size_t fontListPage_=0;
        PinyinDictionary dictionary_;
        TextSession input_;
        Hardware&hw_;
        Settings settings_;
        Reader reader_;
        Transfers transfers_;
        Screen screen_=Screen::Home,returnScreen_=Screen::Home;
        std::string folder_="books",selectedFile_,settingKey_,inputPurpose_,notice_;
        std::vector<FileEntry>files_;
        std::vector<Locator>search_;
        size_t listPage_=0,candidatePage_=0;
        uint64_t revision_=0,presented_=0;
        uint64_t inputEpoch_=0;
        std::vector<Hit>hits_;
        Canvas canvas_;
        bool uppercase_=false,footerDrawn_=false;
        bool glyphLookahead_=false;
        uint64_t glyphPrepared_=0;
        std::string glyphPreparationError_;
        bool full_=true,frameReady_=false,hasPresented_=false,lastGray_=false;
        Status lastStatus_;
        std::string textError_;
        Lease usbLease_,inputLease_;
        mutable std::recursive_mutex mutex_;
        Status draw();
        Status scan();
        Status run(const std::string&,const std::string&);
        void title(const std::string&);
        void footnote(const std::string&);
        void button(Rect,const std::string&,const std::string&,const std::string&value="",bool dark=false,bool enabled=true,int size=22);
        void text(Rect,const std::string&,int size=22,int weight=500,bool inverse=false,int line=32);
        void paragraph(Rect,const std::string&,int size=22,int weight=500,int line=32);
        void home();
        void library();
        void reading();
        void menus();
        void settingsPage();
        std::string effectiveSetting(const std::string&)const;
        void keyboard();
        void transferPage();
        void networkPage();
        void bluetoothPage();
        void usbScreen();
        void uiRow(Rect,const std::string&,const std::string&,const std::string&,const std::string&,const std::string& value="",bool selected=false,bool available=true);
        void pageFooter(size_t page,size_t pages,const std::string& action="list",bool refresh=false);
        std::vector<size_t> settingIndices()const;
        void readerFontPage();
        Status readerFontAction(const std::string&,const std::string&);
        std::string readerFontName(const std::string&);
        bool readerFontScreen()const;
        std::string readerFontSnapshot()const;
        Status beginInput(const std::string&,const std::string&,size_t limit=192,bool secret=false);
        public: Runtime(const std::string&root,Hardware&hardware,Budget budget={},std::function<std::unique_ptr<DocumentSource>(Store&)>source={});
        Status initialize();
        void setFontProgressObserver(std::function<Status(size_t,size_t)> observer){fontCatalog_.setProgressObserver(observer);fonts_.setProgressObserver(std::move(observer));}
        void setDocumentProgressObserver(DocumentProgress observer){reader_.setDocumentProgressObserver(std::move(observer));}
        Status action(const std::string&,const std::string&value="");
        // Fixed keyboard keys may be queued across display revisions, but
        // never across input sessions or layout changes. Candidates stay strict.
        Status inputBatch(uint64_t,const std::vector<std::pair<std::string,std::string>>&);
        // Refresh completed network jobs only while their page is visible.
        bool pollNetwork();
        bool prepareIdle(const std::function<bool()>&stop);
        Status tap(int x,int y,uint64_t observedRevision);
        Status job(DisplayJob&)const;
        Status complete(uint64_t revision,Status result);
        Status suspend();
        Status resume();
        Status prepareUsb();
        Status releaseUsb();
        std::string snapshot()const;
        Store&store() {
            return store_;
        }
        Transfers&transfers() {
            return transfers_;
        }
        ResourceGate&resources() {
            return gate_;
        }
        uint64_t revision()const {
            return revision_;
        }
        bool outputGray()const {
            return reader_.opened()&&((screen_==Screen::Reading&&reader_.fontSpec().gray)||(screen_==Screen::ReaderFonts&&fontDraft_.font.gray));
        }
    };
}
