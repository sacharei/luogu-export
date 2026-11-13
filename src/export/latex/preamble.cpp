// src/export/latex/preamble.cpp
#include "luogu-export/export/latex/preamble.h"

#include <string>

namespace latex::detail
{
namespace
{
// 一栏字体的两种写法：不带 _cjk 的键负责西文，带 _cjk 的键负责中文。
// 两者独立，因为 xeCJK 下 CJK 与 Latin 是两条独立的字体通道。
std::string sibling_key(const std::string &key, const char *suffix)
{
    return key + suffix;
}

// 把西文/中文两部分的字体组合进 target 命令（一个无参数、声明式切换字体的宏）。
// latin / cjk 为空表示该侧不设置；initial 是两侧都不生效时的默认写法
// （一般填空，\texttt 用 \ttfamily）。
//
// 关键点：xeCJK 下中文必须用 \newCJKfontfamily 定义并切换，
// 给 \newfontfamily 加 CJKFont 选项是没有效果的（fontspec 不认识该选项），
// 中文会一直留在文档默认字体上。
//
// 字体名写错时 fontspec 会在导言区直接报错、整份文档无法编译，因此每一侧
// 都先用 \IfFontExistsTF 检查，不存在就退回默认写法并给出警告。
void emit_font_combination(FILE *out, const std::string &target,
                           const std::string &initial,
                           const std::string &latin, const std::string &cjk,
                           const std::string &label)
{
    const std::string lat_cmd = target + "latin";
    const std::string cjk_cmd = target + "cjk";

    // 先给出默认定义，保证正文里的 \luogufont* 始终可用
    std::fputs(("\\newcommand{\\" + target + "}{" + initial + "}\n").c_str(), out);

    auto define_latin = [&]() {
        return "\\newfontfamily{\\" + lat_cmd + "}{" + latin + "}";
    };
    auto define_cjk = [&]() {
        return "\\newCJKfontfamily{\\" + cjk_cmd + "}{" + cjk + "}";
    };
    auto use_latin = [&]() {
        return "\\renewcommand{\\" + target + "}{\\" + lat_cmd + "}";
    };
    auto use_cjk = [&]() {
        return "\\renewcommand{\\" + target + "}{\\" + cjk_cmd + "}";
    };
    auto use_both = [&]() {
        return "\\renewcommand{\\" + target + "}{\\" + lat_cmd + "\\" + cjk_cmd + "}";
    };
    auto warn = [&](const std::string &msg) {
        return "\\PackageWarning{luogu-export}{" + msg + "}";
    };

    if (!latin.empty() && !cjk.empty())
    {
        std::string def;
        def += "\\IfFontExistsTF{" + latin + "}{";
        def += "\\IfFontExistsTF{" + cjk + "}{";
        def += define_latin() + define_cjk() + use_both();
        def += "}{" + define_latin() + use_latin() +
               warn("Font '" + cjk + "' for " + label +
                    "_cjk not found; using only the Latin font") + "}";
        def += "}{";
        def += "\\IfFontExistsTF{" + cjk + "}{";
        def += define_cjk() + use_cjk() +
               warn("Font '" + latin + "' for " + label +
                    " not found; using only the CJK font");
        def += "}{";
        def += warn("Fonts '" + latin + "' and '" + cjk + "' for " + label +
                    " not found; keeping the default font");
        def += "}}";
        def += "\n";
        std::fputs(def.c_str(), out);
    }
    else if (!latin.empty())
    {
        std::fputs(("\\IfFontExistsTF{" + latin + "}{" + define_latin() + use_latin() + "}{" +
                    warn("Font '" + latin + "' for " + label +
                         " not found; keeping the default font") + "}\n").c_str(), out);
    }
    else if (!cjk.empty())
    {
        std::fputs(("\\IfFontExistsTF{" + cjk + "}{" + define_cjk() + use_cjk() + "}{" +
                    warn("Font '" + cjk + "' for " + label +
                         "_cjk not found; keeping the default font") + "}\n").c_str(), out);
    }
}

// 定义一个局部字体宏：\cmd 生效时同时设定西文与中文。
// 只配置了其中一边时，另一边保持当前字体；两边都是 default 则命令为空。
void emit_font_macro(FILE *out, const std::string &cmd, const std::string &family,
                     const settings::Values &fonts)
{
    const std::string latin = settings::raw_value(fonts, family);
    const std::string cjk = settings::raw_value(fonts, sibling_key(family, "_cjk"));
    const bool has_latin = !settings::is_default(latin);
    const bool has_cjk = !settings::is_default(cjk);
    emit_font_combination(out, cmd, "", has_latin ? latin : "",
                          has_cjk ? cjk : "", family);
}

// 写出字体设置块：全局字体 + 各位置局部字体宏。
// 全部命令都是 \def 形式，重复定义不会报错，覆盖已有定义也不需要 \makeatletter。
void emit_fonts(FILE *out, const settings::Values &fonts)
{
    std::fputs("\n% ---- 字体设置（来自 luogu-export/settings.conf 的 [fonts]）----\n", out);

    // 全局字体：值为 default 的键不设置，沿用 ctex 的默认字体。
    // body / body_cjk 排在 main 之后：两者都写时以 body 为准。
    struct GlobalFont
    {
        const char *key;
        const char *command;
    };
    static const GlobalFont kGlobalFonts[] = {
        {"main", "\\setmainfont"},
        {"main_cjk", "\\setCJKmainfont"},
        {"body", "\\setmainfont"},
        {"body_cjk", "\\setCJKmainfont"},
        {"sans", "\\setsansfont"},
        {"sans_cjk", "\\setCJKsansfont"},
        {"mono", "\\setmonofont"},
        {"mono_cjk", "\\setCJKmonofont"},
    };
    for (const GlobalFont &entry : kGlobalFonts)
    {
        const std::string value = settings::raw_value(fonts, entry.key);
        if (settings::is_default(value))
            continue;
        // 字体名写错时同样降级为警告，避免整份文档编译不出来
        const std::string line =
            "\\IfFontExistsTF{" + value + "}{" + entry.command + "{" + value +
            "}}{\\PackageWarning{luogu-export}{Font '" + value + "' for " + entry.key +
            " not found; keeping the default font}}\n";
        std::fputs(line.c_str(), out);
    }

    // \url 默认用等宽族，这里改成跟随当前字体，与 hyperref 默认行为一致；
    // 想给 URL 单独配字体时改 settings.conf 里的 url / url_cjk。
    std::fputs("\\urlstyle{same}\n", out);

    // 各位置的局部字体宏，供正文生成代码调用。
    emit_font_macro(out, "luogufonttitle", "title", fonts);
    emit_font_macro(out, "luogufonttoc", "toc", fonts);
    emit_font_macro(out, "luogufontsection", "section", fonts);
    emit_font_macro(out, "luogufontsubsection", "subsection", fonts);
    emit_font_macro(out, "luogufontsubsubsection", "subsubsection", fonts);
    emit_font_macro(out, "luogufontparagraph", "paragraph", fonts);
    emit_font_macro(out, "luogufontheader", "header", fonts);
    emit_font_macro(out, "luogufontfooter", "footer", fonts);
    emit_font_macro(out, "luogufontquote", "quote", fonts);
    emit_font_macro(out, "luogufontlist", "list", fonts);
    emit_font_macro(out, "luogufonttable", "table", fonts);
    emit_font_macro(out, "luogufonttableheader", "table_header", fonts);
    emit_font_macro(out, "luogufonttablecaption", "table_caption", fonts);
    emit_font_macro(out, "luogufontcallouttitle", "callout_title", fonts);
    emit_font_macro(out, "luogufontcalloutbody", "callout_body", fonts);
    emit_font_macro(out, "luogufontimagecaption", "image_caption", fonts);
    emit_font_macro(out, "luogufontfootnote", "footnote", fonts);
    emit_font_macro(out, "luogufontlink", "link", fonts);
    emit_font_macro(out, "luogufonturl", "url", fonts);
    emit_font_macro(out, "luogufonttag", "tag", fonts);
    emit_font_macro(out, "luogufontcodeblock", "code_block", fonts);
    emit_font_macro(out, "luogufontcodekeyword", "code_keyword", fonts);
    emit_font_macro(out, "luogufontcodecomment", "code_comment", fonts);
    emit_font_macro(out, "luogufontcodestring", "code_string", fonts);
    emit_font_macro(out, "luogufontcodenumber", "code_number", fonts);
    emit_font_macro(out, "luogufontbold", "bold", fonts);

    // 行内代码与 \verb 的字体：键未配置时退回等宽字体（mono / mono_cjk），
    // 这样只设置 mono 就能同时影响行内代码，保持旧行为。
    const std::string code_inline = settings::raw_value(fonts, "code_inline");
    const std::string code_inline_cjk = settings::raw_value(fonts, "code_inline_cjk");
    const bool has_inline = !settings::is_default(code_inline);
    const bool has_inline_cjk = !settings::is_default(code_inline_cjk);

    // \texttt 是我们自己生成代码文本时用的命令（行内代码、\verb、数学里的
    // 等宽片段），这里把它指向行内代码字体。
    //
    // 必须用 \DeclareTextFontCommand：它生成的命令按「声明式字体切换」展开，
    // 能正确处理参数里出现的 $，从而兼容题面里 {$\texttt{A$\sim$ G}$} 这类
    // 畸形写法（数学与行内代码互相嵌套）。若改成
    // \renewcommand{\texttt}[1]{{<font> #1}}，多的那层分组会破坏定界符配对，
    // 让这些题面报 "Missing $ inserted"。
    //
    // 默认按 \ttfamily（= mono）定义；配置了字体时再用组合宏覆盖，
    // 中文部分由 \newCJKfontfamily 切换（CJKFont 选项无效）。
    std::fputs("\\DeclareTextFontCommand{\\texttt}{\\ttfamily}\n", out);
    if (has_inline || has_inline_cjk)
    {
        emit_font_combination(out, "luogufontcodeinlinedecl", "\\ttfamily",
                              has_inline ? code_inline : "",
                              has_inline_cjk ? code_inline_cjk : "",
                              "code_inline");
        std::fputs("\\DeclareTextFontCommand{\\texttt}{\\luogufontcodeinlinedecl}\n", out);
    }

    // 粗体：\\textbf 使用 [fonts] 的 bold / bold_cjk，并保留 \\bfseries
    // （字体自带粗体时用粗体字形）。两者都是 default 时 \\luogufontbold 为空，
    // 等同于标准的 \\bfseries。
    std::fputs("\\DeclareTextFontCommand{\\textbf}{\\luogufontbold\\bfseries}\n", out);
    
    // \maketitle / \tableofcontents 的内容在导言区捕获不到，只能把命令本身
    // 包一层字体（先存原名再重定义，避免递归）。
    std::fputs("\\let\\luoguoriginmaketitle\\maketitle\n", out);
    std::fputs("\\renewcommand{\\maketitle}{\\luogufonttitle\\luoguoriginmaketitle}\n", out);
    std::fputs("\\let\\luoguorigintableofcontents\\tableofcontents\n", out);
    std::fputs("\\renewcommand{\\tableofcontents}{\\luogufonttoc\\luoguorigintableofcontents}\n", out);
}
} // namespace

void write_preamble(FILE *out, const settings::Values &fonts)
{
    std::fputs("\\documentclass{book}\n", out);
    std::fputs("\\usepackage[UTF8]{ctex}\n", out);
    std::fputs("\\usepackage{graphicx}\n", out);
    std::fputs("\\usepackage{titlesec}\n", out);
    std::fputs("\\usepackage{fancyhdr}\n", out);
    std::fputs("\\usepackage[hidelinks]{hyperref}\n", out);
    std::fputs("\\usepackage[normalem]{ulem}\n", out);
    std::fputs("\\usepackage{amsmath,amssymb}\n", out);
    std::fputs("\\usepackage{mathtools}\n", out);
    std::fputs("\\usepackage{bm}\n", out);
    std::fputs("\\usepackage{mathrsfs}\n", out);
    std::fputs("\\usepackage{xcolor}\n", out);
    std::fputs("\\usepackage{listings}\n", out);
    std::fputs("\\usepackage{cancel}\n", out);
    std::fputs("\\usepackage{geometry}\n", out);
    std::fputs("\\usepackage{tabularx}\n", out);
    std::fputs("\\usepackage{array}\n", out);
    std::fputs("\\usepackage{multirow}\n", out);
    // booktabs：::cute-table{tuack}/{three} 的上下粗线、表头细线
    std::fputs("\\usepackage{booktabs}\n", out);
    
    // 字体设置：全局字体 + 各位置局部字体宏（来自 settings.conf）
    emit_fonts(out, fonts);

    // framed：折叠框展开后左侧的彩色竖线。用 \MakeFramed 逐页绘制 \vrule，
    // 而不是把整段内容包进 tcolorbox 的彩色盒子：tcolorbox 的 breakable
    // 与 lstlisting、\color 混用时，跨页会破坏 xdvipdfmx 的颜色栈
    // （后续正文整段变白），并且会把代码框从中间劈开造成重叠。
    // framed 的竖线逐行绘制，可以安全跨页，也不会影响内容自身的颜色。
    std::fputs("\\usepackage{framed}\n", out);
    std::fputs("\\setlength{\\OuterFrameSep}{6pt}\n", out);
    std::fputs("\\geometry{margin=2cm}\n", out);
        // 去掉所有章节序号（\section 等）：目录和正文都不显示数字
    std::fputs("\\setcounter{secnumdepth}{-1}\n", out);

    std::fputs("\\pagestyle{fancy}\n", out);
    std::fputs("\\fancyhf{}\n", out);
    std::fputs("\\fancyhead[LE]{\\luogufontheader\\thepage}\n", out);
    std::fputs("\\fancyhead[RE]{\\nouppercase{\\luogufontheader\\rightmark}}\n", out);
    std::fputs("\\fancyhead[LO]{\\nouppercase{\\luogufontheader\\rightmark}}\n", out);
    std::fputs("\\fancyhead[RO]{\\luogufontheader\\thepage}\n", out);
    std::fputs("\\fancyfoot[LE,RO]{\\luogufontfooter}\n", out);
    std::fputs("\\fancyfoot[LO,RE]{\\luogufontfooter}\n", out);

    std::fputs("\\titleformat{\\section}\n", out);
    std::fputs("{\\luogufontsection\\Large}\n", out);
    std::fputs("{}\n", out);
    std::fputs("{0em}{}\n", out);
    std::fputs("\\titleformat{\\subsection}\n", out);
    std::fputs("{\\luogufontsubsection\\large}\n", out);
    std::fputs("{}\n", out);
    std::fputs("{0em}{}\n", out);
    std::fputs("\\titleformat{\\subsubsection}\n", out);
    std::fputs("{\\luogufontsubsubsection\\color{gray}}\n", out);
    std::fputs("{}\n", out);
    std::fputs("{1em}{}\n", out);
    std::fputs("\\titleformat{\\paragraph}\n", out);
    std::fputs("{\\luogufontparagraph\\bfseries}\n", out);
    std::fputs("{}\n", out);
    std::fputs("{0em}{}\n", out);

    std::fputs("\\lstset{\n", out);
    std::fputs("    breaklines=true,\n", out);
    std::fputs("    breakatwhitespace=false,\n", out);
    std::fputs("    keepspaces=true,\n", out);
    std::fputs("    showstringspaces=false,\n", out);
    std::fputs("    tabsize=4,\n", out);
    std::fputs("    keywordstyle=\\color{blue}\\bfseries\\luogufontcodekeyword,\n", out);
    std::fputs("    commentstyle=\\color{green!50!black}\\luogufontcodecomment,\n", out);
    std::fputs("    stringstyle=\\color{red!60!black}\\luogufontcodestring,\n", out);
    std::fputs("    frame=single,\n", out);
    std::fputs("    columns=flexible,\n", out);
    std::fputs("    numbers=left,\n", out);
    std::fputs("    numberstyle=\\footnotesize\\luogufontcodenumber\\color{gray},\n", out);
    std::fputs("    basicstyle=\\small\\luogufontcodeblock,\n", out);
    std::fputs("    rulecolor=\\color{blue},\n", out);
    std::fputs("}\n", out);

        // 当前 TeX Live 的 listings 没有这些语言，手动补上，否则
        // \begin{lstlisting}[language=Rust] 会报 "Couldn't load requested language"
    std::fputs("\\lstdefinelanguage{Rust}{\n", out);
    std::fputs("    morekeywords={as,async,await,break,const,continue,crate,dyn,else,enum,", out);
    std::fputs("extern,false,fn,for,if,impl,in,let,loop,match,mod,move,mut,pub,ref,return,", out);
    std::fputs("self,Self,static,struct,super,trait,true,type,unsafe,use,where,while,yield},\n", out);
    std::fputs("  morecomment=[l]{//},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{JavaScript}{\n", out);
    std::fputs("  morekeywords={abstract,arguments,await,boolean,break,byte,case,catch,char,", out);
    std::fputs("class,const,continue,debugger,default,delete,do,double,else,enum,eval,export,", out);
    std::fputs("extends,false,final,finally,float,for,function,goto,if,implements,import,in,", out);
    std::fputs("instanceof,int,interface,let,long,native,new,null,package,private,protected,", out);
    std::fputs("public,return,short,static,super,switch,synchronized,this,throw,throws,", out);
    std::fputs("transient,true,try,typeof,var,void,volatile,while,with,yield},\n", out);
    std::fputs("  morecomment=[l]{//},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("  morestring=[b]`,\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{TypeScript}{\n", out);
    std::fputs("  morekeywords={abstract,any,as,asserts,async,await,boolean,break,case,catch,", out);
    std::fputs("class,const,continue,debugger,declare,default,delete,do,else,enum,export,", out);
    std::fputs("extends,false,finally,for,from,function,get,if,implements,import,in,infer,", out);
    std::fputs("instanceof,interface,is,keyof,let,module,namespace,never,new,null,number,", out);
    std::fputs("object,of,package,private,protected,public,readonly,return,set,static,string,", out);
    std::fputs("super,switch,symbol,this,throw,true,try,type,typeof,undefined,unknown,var,", out);
    std::fputs("void,while,with,yield},\n", out);
    std::fputs("  morecomment=[l]{//},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("  morestring=[b]`,\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{CSharp}{\n", out);
    std::fputs("  morekeywords={abstract,as,base,bool,break,byte,case,catch,char,checked,", out);
    std::fputs("class,const,continue,decimal,default,delegate,do,double,else,enum,event,", out);
    std::fputs("explicit,extern,false,finally,fixed,float,for,foreach,goto,if,implicit,in,", out);
    std::fputs("int,interface,internal,is,lock,long,namespace,new,null,object,operator,out,", out);
    std::fputs("override,params,private,protected,public,readonly,ref,return,sbyte,sealed,", out);
    std::fputs("short,sizeof,stackalloc,static,string,struct,switch,this,throw,true,try,", out);
    std::fputs("typeof,uint,ulong,unchecked,unsafe,ushort,using,virtual,void,volatile,while},\n", out);
    std::fputs("  morecomment=[l]{//},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{CSS}{\n", out);
    std::fputs("  morekeywords={align-items,align-self,animation,background,background-color,", out);
    std::fputs("border,border-radius,bottom,box-shadow,color,content,cursor,display,flex,", out);
    std::fputs("float,font,font-family,font-size,font-weight,grid,gap,height,justify-content,", out);
    std::fputs("left,line-height,list-style,margin,max-height,max-width,min-height,min-width,", out);
    std::fputs("opacity,overflow,padding,position,right,text-align,text-decoration,top,", out);
    std::fputs("transform,transition,visibility,width,z-index},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{Lua}{\n", out);
    std::fputs("  morekeywords={and,break,do,else,elseif,end,false,for,function,goto,if,in,", out);
    std::fputs("local,nil,not,or,repeat,return,then,true,until,while},\n", out);
    std::fputs("  morecomment=[l]{--},\n", out);
    std::fputs("  morecomment=[s]{--[[}{]]},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("}\n", out);
    std::fputs("\\colorlet{Red}{red}\\colorlet{Green}{green}\\colorlet{Blue}{blue}\n", out);
    std::fputs("\\colorlet{Orange}{orange}\\colorlet{Pink}{pink}\\colorlet{Purple}{purple}\n", out);
    std::fputs("\\colorlet{Cyan}{cyan}\\colorlet{Brown}{brown}\\colorlet{Teal}{teal}\n", out);
    std::fputs("\\colorlet{Violet}{violet}\\colorlet{White}{white}\\colorlet{Black}{black}\n", out);
    std::fputs("\\colorlet{Grey}{gray}\\colorlet{grey}{gray}\\colorlet{Gray}{gray}\n", out);
    std::fputs("\\colorlet{default}{black}\n", out);
    std::fputs("\\colorlet{normal}{black}\n", out);
    std::fputs("\\colorlet{transparent}{white}\n", out);
    std::fputs("\\definecolor{Aquamarine}{RGB}{127,255,212}\n", out);
    std::fputs("\\definecolor{gold}{RGB}{255,215,0}\n", out);
    std::fputs("\\providecommand{\\degree}{^{\\circ}}\n", out);
    std::fputs("\\providecommand{\\exist}{\\exists}\n", out);
    std::fputs("\\providecommand{\\infin}{\\infty}\n", out);
    std::fputs("\\providecommand{\\sube}{\\subseteq}\n", out);
    std::fputs("\\providecommand{\\supe}{\\supseteq}\n", out);
    std::fputs("\\providecommand{\\lang}{\\langle}\n", out);
    std::fputs("\\providecommand{\\rang}{\\rangle}\n", out);
    std::fputs("\\providecommand{\\rarr}{\\rightarrow}\n", out);
    std::fputs("\\providecommand{\\larr}{\\leftarrow}\n", out);
    std::fputs("\\providecommand{\\uarr}{\\uparrow}\n", out);
    std::fputs("\\providecommand{\\darr}{\\downarrow}\n", out);
    std::fputs("\\providecommand{\\lrarr}{\\leftrightarrow}\n", out);
    std::fputs("\\providecommand{\\xlongequal}[2][=]{\\overset{#2}{#1}}\n", out);
    std::fputs("\\providecommand{\\argmax}{\\operatorname*{arg\\,max}}\n", out);
    std::fputs("\\providecommand{\\argmin}{\\operatorname*{arg\\,min}}\n", out);
    std::fputs("\\providecommand{\\ctg}{\\cot}\n", out);
    std::fputs("\\providecommand{\\lt}{<}\n", out);
    std::fputs("\\providecommand{\\gt}{>}\n", out);
    std::fputs("\\providecommand{\\Alpha}{\\mathrm{A}}\n", out);
    std::fputs("\\providecommand{\\Beta}{\\mathrm{B}}\n", out);
    std::fputs("\\providecommand{\\Epsilon}{\\mathrm{E}}\n", out);
    std::fputs("\\providecommand{\\Zeta}{\\mathrm{Z}}\n", out);
    std::fputs("\\providecommand{\\Eta}{\\mathrm{H}}\n", out);
    std::fputs("\\providecommand{\\Iota}{\\mathrm{I}}\n", out);
    std::fputs("\\providecommand{\\Kappa}{\\mathrm{K}}\n", out);
    std::fputs("\\providecommand{\\Mu}{\\mathrm{M}}\n", out);
    std::fputs("\\providecommand{\\Nu}{\\mathrm{N}}\n", out);
    std::fputs("\\providecommand{\\Omicron}{\\mathrm{O}}\n", out);
    std::fputs("\\providecommand{\\Rho}{\\mathrm{P}}\n", out);
    std::fputs("\\providecommand{\\Tau}{\\mathrm{T}}\n", out);
    std::fputs("\\providecommand{\\Upsilon}{\\mathrm{Y}}\n", out);
    std::fputs("\\providecommand{\\Chi}{\\mathrm{X}}\n", out);
    std::fputs("\\providecommand{\\R}{\\mathbb{R}}\n", out);
    std::fputs("\\providecommand{\\N}{\\mathbb{N}}\n", out);
    std::fputs("\\providecommand{\\Z}{\\mathbb{Z}}\n", out);
    std::fputs("\\providecommand{\\Q}{\\mathbb{Q}}\n", out);
    std::fputs("\\providecommand{\\C}{\\mathbb{C}}\n", out);
    std::fputs("\\providecommand{\\red}[1]{\\textcolor{red}{#1}}\n", out);
    std::fputs("\\providecommand{\\blue}[1]{\\textcolor{blue}{#1}}\n", out);
    std::fputs("\\providecommand{\\green}[1]{\\textcolor{green}{#1}}\n", out);
    std::fputs("\\providecommand{\\pink}[1]{\\textcolor{pink}{#1}}\n", out);
    std::fputs("\\providecommand{\\orange}[1]{\\textcolor{orange}{#1}}\n", out);
    std::fputs("\\providecommand{\\purple}[1]{\\textcolor{purple}{#1}}\n", out);
    std::fputs("\\providecommand{\\brown}[1]{\\textcolor{brown}{#1}}\n", out);
    std::fputs("\\providecommand{\\gray}[1]{\\textcolor{gray}{#1}}\n", out);
    std::fputs("\\providecommand{\\cyan}[1]{\\textcolor{cyan}{#1}}\n", out);
    std::fputs("\\providecommand{\\teal}[1]{\\textcolor{teal}{#1}}\n", out);
    std::fputs("\\providecommand{\\magenta}[1]{\\textcolor{magenta}{#1}}\n", out);
    std::fputs("\\providecommand{\\yellow}[1]{\\textcolor{yellow}{#1}}\n", out);
    std::fputs("\\providecommand{\\violet}[1]{\\textcolor{violet}{#1}}\n", out);
    std::fputs("\\lstdefinelanguage{JavaScript}{\n", out);
    std::fputs("keywords={break, case, catch, class, const, continue, debugger, default, delete, do, else, export, extends, finally, for, function, if, import, in, instanceof, let, new, return, super, switch, this, throw, try, typeof, var, void, while, with, yield, await, async, of, from, as},", out);
    std::fputs("keywordstyle=\\color{blue}\\bfseries\\luogufontcodekeyword,\n", out);
    std::fputs("ndkeywords={boolean, number, string, null, undefined, true, false},\n", out);
    std::fputs("ndkeywordstyle=\\color{red}\\bfseries\\luogufontcodekeyword,\n", out);
    std::fputs("identifierstyle=\\color{black},\n", out);
    std::fputs("sensitive=false,\n", out);
    std::fputs("comment=[l]{//},\n", out);
    std::fputs("morecomment=[s]{/*}{*/},", out);
    std::fputs("commentstyle=\\color{green}\\luogufontcodecomment,\n", out);
    std::fputs("stringstyle=\\color{purple}\\luogufontcodestring,\n", out);
    std::fputs("morestring=[b]',\n", out);
    std::fputs("morestring=[b]\"\n", out);
    std::fputs("}\n", out);
    std::fputs("\\title{luogu export}\n\\author{luogu-export}\n\\date{\\today}\n", out);

    // 图片按像素尺寸（CSS 像素，1px = 0.75pt）排版，仅在超出正文宽度/高度时
    // 按比例缩小。XeTeX 对没有 DPI 的位图按 72dpi 计算（把像素数直接当成 pt），
    // 会让图片显得过大甚至超出页面，因此这里显式给定像素换算出的自然宽度。
    // #1 图片路径；#2 自然宽度（pt，0 表示无法得到像素尺寸，如 PDF/EPS/
    // 解析失败，此时回退到 graphicx 的自然尺寸，并由后续判断兜底缩放）。
    std::fputs("\\newsavebox{\\luoguimgbox}\n", out);
    std::fputs("\\newcommand{\\luoguimg}[2]{%\n", out);
    std::fputs("  \\IfFileExists{#1}{%\n", out);
    std::fputs("    \\ifdim#2pt>0pt%\n", out);
    std::fputs("      \\sbox\\luoguimgbox{\\includegraphics[width=#2pt]{#1}}%\n", out);
    std::fputs("    \\else%\n", out);
    std::fputs("      \\sbox\\luoguimgbox{\\includegraphics{#1}}%\n", out);
    std::fputs("    \\fi%\n", out);
    std::fputs("    \\ifdim\\wd\\luoguimgbox>\\linewidth%\n", out);
    std::fputs("      \\sbox\\luoguimgbox{\\includegraphics[width=\\linewidth]{#1}}%\n", out);
    std::fputs("    \\fi%\n", out);
    std::fputs("    \\ifdim\\dimexpr\\ht\\luoguimgbox+\\dp\\luoguimgbox\\relax>\\dimexpr\\textheight-2\\baselineskip\\relax%\n", out);
    std::fputs("      \\sbox\\luoguimgbox{\\includegraphics[height=\\dimexpr\\textheight-2\\baselineskip\\relax]{#1}}%\n", out);
    std::fputs("    \\fi%\n", out);
    std::fputs("    \\usebox\\luoguimgbox%\n", out);
    std::fputs("  }{\\mbox{}}%\n", out);
    std::fputs("}%\n", out);


    // 折叠框（:::info / :::success / :::warning / :::error）：洛谷上默认折叠，
    // 导出时一律展开（忽略 {open}），并保留标题和左侧的彩色竖线。
    // 颜色取自洛谷站点的折叠框配色。
    //
    // 竖线由 framed 的 \MakeFramed 在每页的左侧逐行画一条 \vrule，
    // 内容本身不进入任何颜色组。这样：
    //   - 跨页时不会与 tcolorbox 的 pdf:bcolor/pdf:ecolor 交叉 pop，
    //     避免 xdvipdfmx 颜色栈里残留白色导致后续正文全部变白；
    //   - 代码框（lstlisting）不会在分页处被劈开而与后续内容重叠。
    // \begingroup/\endgroup 只包住环境自身，避免 \FrameCommand 泄漏到正文。
    const char *kCalloutColors[][2] = {
        {"info", "luoguinfo"},
        {"success", "luogusuccess"},
        {"warning", "luoguwarning"},
        {"error", "luoguerror"},
    };
    std::fputs("\\definecolor{luoguinfo}{RGB}{46,141,214}\n", out);
    std::fputs("\\definecolor{luogusuccess}{RGB}{72,188,25}\n", out);
    std::fputs("\\definecolor{luoguwarning}{RGB}{255,185,22}\n", out);
    std::fputs("\\definecolor{luoguerror}{RGB}{227,67,53}\n", out);
    for (const auto &entry : kCalloutColors)
    {
        const std::string env = "luogucallout" + std::string(entry[0]);
        // \FrameCommand 只给竖线着色：颜色放在独立的分组里，紧跟其后的
        // 内容盒子仍沿用正文颜色。
        const std::string def =
            "\\newenvironment{" + env + "}{%\n"
            "\\begingroup\n"
            "\\def\\FrameCommand{{\\color{" + std::string(entry[1]) +
            "}\\vrule width 3pt}\\hspace{10pt}}%\n"
            "\\MakeFramed{\\advance\\hsize-\\width\\FrameRestore}%\n"
            "}{%\n"
            "\\endMakeFramed\n"
            "\\endgroup\n"
            "}\n";
        std::fputs(def.c_str(), out);
    }

    // ::cute-table（tuack / three）的公共排版：自然宽度居中。
    // booktabs 默认在横线上下各留 aboverulesep/belowrulesep 的空白，列间的
    // 竖线会在这段空白里断开（洛谷上是连通的），这里把两段间距清零；
    // 行高改由 \arraystretch 撑开（1.7 ≈ 站点上约 27.75pt 的行距），
    // 既贴近站点样式又保证竖线与横线相接。
    // \arrayrulewidth 控制列间细竖线的粗细：洛谷 CSS 为 1px，即 0.75pt
    // （上下粗线 5px、表头线 3px 的宽度在 table.cpp 里逐条给出）。
    std::fputs("\\newenvironment{luogucutable}{%\n", out);
    std::fputs("\\begin{center}%\n", out);
    std::fputs("\\begingroup\n", out);
    std::fputs("\\renewcommand{\\arraystretch}{1.7}%\n", out);
    std::fputs("\\setlength{\\arrayrulewidth}{0.75pt}%\n", out);
    std::fputs("\\setlength{\\aboverulesep}{0pt}%\n", out);
    std::fputs("\\setlength{\\belowrulesep}{0pt}%\n", out);
    std::fputs("}{%\n", out);
    std::fputs("\\endgroup\n", out);
    std::fputs("\\end{center}%\n", out);
    std::fputs("}\n", out);
}
} // namespace latex::detail
