#pragma once
#include <string>
namespace tabnamp {
// Delimited text needs no locale or iostream initialization before FreeRTOS.
class TextReader {
    const std::string& text_;
    size_t position_=0;
public:
    explicit TextReader(const std::string& text):text_(text){}
    bool next(std::string& out,char delimiter='\n') {
        if(position_>=text_.size())return false;
        auto end=text_.find(delimiter,position_);
        if(end==std::string::npos)end=text_.size();
        out.assign(text_,position_,end-position_);
        position_=end==text_.size()?end:end+1;
        return true;
    }
};
}
