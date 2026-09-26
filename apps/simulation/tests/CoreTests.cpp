#include "core/Geometry.h"
#include "core/Project.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
void rejects(const std::function<void()>& fn,const char* message) {
    bool threw=false;try {fn();} catch(const std::exception&) {threw=true;}require(threw,message);
}
}
int main() {
    try {
        using namespace evolve;
        require(parseSequence(">Example\r\nat gc\r\nTTAc\n")=="ATGCTTAC","FASTA normalization");
        require(parseSequence("acgt")=="ACGT","plain sequence normalization");
        rejects([] {parseSequence(">one\nACGT\n>two\nACGT");},"reject multiple FASTA records");
        rejects([] {parseSequence("ACGN");},"reject unknown bases");
        rejects([] {parseSequence("ACG");},"minimum length");
        rejects([] {parseSequence(std::string(MaxBases+1,'A'));},"maximum length");
        require(parseSequence(std::string(MaxBases,'A')).size()==MaxBases,"maximum supported input");
        require(complement('A')=='T' && complement('G')=='C',"complement mapping");
        require(gcContent("ACGT")==50.0,"GC percentage");
        Project p;const auto original=p.baseline();
        require(p.edit(0,'C'),"edit changes sequence");
        require(p.baseline()==original && p.sequence()[0]=='C',"baseline immutable");
        auto revision=p.revision();require(!p.edit(0,'C') && p.revision()==revision,"same-base edit is a no-op");
        auto stale=p.analyze();p.edit(1,'G');require(!p.accept(stale),"stale analysis rejected");
        auto current=p.analyze();require(current.edits==2 && p.accept(current) && p.hasResult(),"current result accepted");
        require(p.undo() && !p.hasResult() && p.sequence()[1]==original[1],"undo invalidates result");
        require(p.redo() && p.sequence()[1]=='G',"redo restores edit");
        p.undo();p.edit(2,'A');require(!p.canRedo(),"new edit clears redo branch");
        const auto saved=p.serialize();Project copy;copy.deserialize(saved);
        require(copy.sequence()==p.sequence() && copy.baseline()==p.baseline(),"project round trip");
        require(!copy.hasResult() && !copy.canUndo(),"reopened project starts without transient results/history");
        rejects([&] {copy.deserialize("EVOLVE_PROJECT 2\nACGT\nACGT\n");},"unsupported version rejected");
        rejects([&] {copy.deserialize("EVOLVE_PROJECT 0.1\nACGT\nACGTA\n");},"length mismatch rejected");
        rejects([&] {copy.deserialize("EVOLVE_PROJECT 0.1\nACGT\n");},"truncated project rejected");
        require(copy.serialize()==saved,"failed load does not replace project");
        rejects([&] {copy.importSequence("invalid");},"invalid import rejected");
        require(copy.serialize()==saved,"failed import preserves project");
        rejects([&] {p.edit(MaxBases,'A');},"out of range edit rejected");
        require(p.restoreBaseline() && p.sequence()==original,"restore baseline");
        require(p.undo() && p.sequence()!=original,"restore is undoable");
        auto mesh=buildHelix(copy.sequence(),copy.baseline(),0,false,false);
        auto compared=buildHelix(copy.sequence(),copy.baseline(),0,true,true);
        require(!mesh.vertices.empty() && mesh.vertices.size()%3==0,"triangle mesh generated");
        require(compared.vertices.size()>mesh.vertices.size(),"comparison and grid add geometry");
        require(mesh.pickPoints.size()==copy.sequence().size()*2,"each displayed base is selectable");
        bool finite=true;
        for(const auto& v:compared.vertices) finite=finite && std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z)
            && std::isfinite(v.normal.x) && std::isfinite(v.normal.y) && std::isfinite(v.normal.z);
        require(finite,"finite mesh positions and normals");
        auto big=buildHelix(std::string(MaxBases,'A'),std::string(MaxBases,'A'),0,true,true);
        require(big.vertices.size()<2000000,"bounded maximum geometry allocation");
        rejects([] {buildHelix("ACGT","ACG",0,false,false);},"mismatched geometry inputs rejected");
        std::cout<<"PASS: "<<checks<<" checks\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
