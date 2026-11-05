#include "../OfficeJournal.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

using Json=nlohmann::json;
static void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static std::vector<Json> read(const std::filesystem::path& path) {
    std::ifstream file(path);std::string line;std::vector<Json> result;
    while(std::getline(file,line)) result.push_back(Json::parse(line));
    return result;
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        if(argc==3 && std::string(argv[1])=="--abrupt") {
            office::Journal journal(std::filesystem::path(QString::fromUtf8(argv[2]).toStdWString()));
            journal.write("capture_create_begin",{{"source_hwnd",123}});
            // Bypass C++/Qt teardown: the parent must still see this last intent.
            ExitProcess(0);
        }
        QTemporaryDir temp;require(temp.isValid(),"Temporary journal directory");
        auto root=std::filesystem::path(temp.path().toStdWString());
        auto path=root/"abrupt.jsonl";
        QProcess child;child.start(app.applicationFilePath(),{"--abrupt",QString::fromStdWString(path.wstring())});
        require(child.waitForFinished(-1) && child.exitCode()==0,"Abrupt child completed");
        auto records=read(path);require(records.size()==1 && records[0]["event"]=="capture_create_begin","Write survives abrupt exit without destructor/flush timer");
        for(auto key:{"utc","elapsed_ms","pid","tid","session","sequence","build"}) require(records[0].contains(key),"Correlation field missing");
        {
            office::Journal journal(path);
            journal.write("next_process");
        }
        require(read(path).size()==2,"Restart appends instead of truncating prior session");
        {
            office::Journal journal(root/"parallel.jsonl");
            std::vector<std::thread> workers;
            for(int t=0;t<4;++t) workers.emplace_back([&,t]{for(int i=0;i<40;++i)journal.write("callback",{{"worker",t},{"index",i}});});
            for(auto& worker:workers)worker.join();
            require(journal.error().empty(),"Concurrent journal writes succeeded");
        }
        records=read(root/"parallel.jsonl");std::set<unsigned> seq;
        for(auto& record:records)seq.insert(record["sequence"]);
        require(records.size()==160 && seq.size()==160,"Concurrent writes retain complete noninterleaved records");
        auto rotated=root/"rotating.jsonl";
        {office::Journal journal(rotated,2048,3);for(int i=0;i<100;++i)journal.write("rotate",{{"index",i}});require(journal.error().empty(),"Rotation succeeded");}
        require(read(rotated).back()["data"]["index"]==99,"Newest rotation record retained");
        require(std::filesystem::exists(rotated.wstring()+L".2") && !std::filesystem::exists(rotated.wstring()+L".3"),"Bounded rotation retains exactly current plus two archives");
        for(auto& entry:std::filesystem::directory_iterator(root)) if(entry.path().filename().wstring().starts_with(L"rotating")) {
            require(entry.file_size()<=2048,"Segment byte limit respected");read(entry.path());
        }
        {office::Journal blocked(root);blocked.write("cannot_open");require(!blocked.error().empty(),"I/O failure visible without crashing lighting owner");}
        std::cout<<"Journal abrupt exit, append, concurrency, retention and error checks passed\n";
        return 0;
    } catch(std::exception const& e) {std::cerr<<e.what()<<'\n';return 1;}
}
