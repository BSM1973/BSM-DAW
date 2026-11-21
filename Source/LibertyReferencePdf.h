#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
namespace LibertyReference
{
// Portable PDF 1.4 summary, Helvetica WinAnsi, safe literal escaping and xref.
inline std::string pdfLiteral(const std::string& utf8)
{
    std::string out;
    for(std::size_t i=0;i<utf8.size();)
    {
        unsigned int c=static_cast<unsigned char>(utf8[i++]);
        unsigned int remaining=0;
        if(c>=0xc0&&c<=0xdf){c&=31;remaining=1;}
        else if(c>=0xe0&&c<=0xef){c&=15;remaining=2;}
        else if(c>=0xf0&&c<=0xf7){c&=7;remaining=3;}
        while(remaining&&i<utf8.size()){c=(c<<6)|(static_cast<unsigned char>(utf8[i++])&63);--remaining;}
        if(c==0x2019)c=0x92;else if(c==0x2013)c=0x96;else if(c==0x2014)c=0x97;else if(c==0x0153)c=0x9c;else if(c==0x0152)c=0x8c;
        if(c>255){std::ostringstream u;u<<"[U+"<<std::uppercase<<std::hex<<c<<"]";out+=u.str();continue;}
        if(c=='('||c==')'||c=='\\'){out+='\\';out+=char(c);}
        else if(c>=32&&c<=126)out+=char(c);
        else {out+='\\';out+=char('0'+((c>>6)&7));out+=char('0'+((c>>3)&7));out+=char('0'+(c&7));}
    }
    return out;
}
inline std::string makePdf(const std::vector<std::string>& lines)
{
    // 45 lines/page; caller wraps long text before serialization.
    constexpr std::size_t perPage=45;
    const std::size_t pages=std::max<std::size_t>(1,(lines.size()+perPage-1)/perPage);
    std::vector<std::string> objects;
    objects.emplace_back("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids="<< /Type /Pages /Kids [";
    for(std::size_t p=0;p<pages;++p)kids+=std::to_string(4+p*2)+" 0 R ";
    kids+="] /Count "+std::to_string(pages)+" >>";objects.push_back(kids);
    objects.emplace_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    for(std::size_t p=0;p<pages;++p)
    {
        const auto streamId=5+p*2;
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R >> >> /Contents "+std::to_string(streamId)+" 0 R >>");
        std::string stream="0.05 0.12 0.2 rg 0 780 595 62 re f\n1 1 1 rg BT /F1 17 Tf 42 805 Td (LIBERTY | REFERENCE STUDIO) Tj ET\n0.08 0.12 0.18 rg BT /F1 10 Tf 14 TL 42 754 Td\n";
        for(std::size_t i=p*perPage;i<std::min(lines.size(),(p+1)*perPage);++i)stream+="("+pdfLiteral(lines[i])+") Tj T*\n";
        stream+="ET\nBT /F1 9 Tf 42 38 Td (Mesures indicatives - page "+std::to_string(p+1)+" / "+std::to_string(pages)+") Tj ET\n";
        objects.push_back("<< /Length "+std::to_string(stream.size())+" >>\nstream\n"+stream+"endstream");
    }
    std::string file="%PDF-1.4\n";std::vector<std::size_t> offsets{0};
    for(std::size_t i=0;i<objects.size();++i){offsets.push_back(file.size());file+=std::to_string(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}
    const auto start=file.size();std::ostringstream cross;cross<<"xref\n0 "<<offsets.size()<<"\n0000000000 65535 f \n";
    for(std::size_t i=1;i<offsets.size();++i)cross<<std::setfill('0')<<std::setw(10)<<offsets[i]<<" 00000 n \n";
    cross<<"trailer\n<< /Size "<<offsets.size()<<" /Root 1 0 R >>\nstartxref\n"<<start<<"\n%%EOF\n";return file+cross.str();
}
}
