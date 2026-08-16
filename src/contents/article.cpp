// src/contents/article.cpp
#include <cstdio>
#include <string>
#include <limits>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/HTMLparser.h>
#include <libxml2/libxml/xpath.h>
#include <nlohmann/json.hpp>
#include "luogu-export/contents/article.h"
#include "luogu-export/util/image_util.h"

using nlohmann::json;
using article::Article;

article::Article::Article() { return; }

article::Article::Article(std::string html)
{
    if (html.empty())
        return;

    if (html.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        std::fprintf(stderr, "HTML content is too large to parse\n");
        return;
    }

    // 1. 解析 HTML
    htmlDocPtr doc = htmlReadMemory(html.c_str(), static_cast<int>(html.size()), nullptr, "UTF-8", HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc) return;

    // 2. 使用 XPath 查找 id="lentille-context" 的 script 标签
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    if (!xpathCtx)
    {
        xmlFreeDoc(doc);
        return;
    }
    xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(BAD_CAST "//script[@id='lentille-context']", xpathCtx);
    if (!xpathObj || !xpathObj->nodesetval || xpathObj->nodesetval->nodeNr == 0)
    {
        xmlXPathFreeObject(xpathObj);
        xmlXPathFreeContext(xpathCtx);
        xmlFreeDoc(doc);
        return;
    }

    // 3. 获取 script 标签内的文本内容
    xmlNodePtr node = xpathObj->nodesetval->nodeTab[0];
    if (!node)
    {
        xmlXPathFreeObject(xpathObj);
        xmlXPathFreeContext(xpathCtx);
        xmlFreeDoc(doc);
        return;
    }
    xmlChar *xmlContent = xmlNodeGetContent(node);
    if (!xmlContent)
    {
        xmlXPathFreeObject(xpathObj);
        xmlXPathFreeContext(xpathCtx);
        xmlFreeDoc(doc);
        return;
    }
    std::string jsonStr(reinterpret_cast<const char *>(xmlContent));
    xmlFree(xmlContent);
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    xmlFreeDoc(doc);

    // 4. 解析 JSON
    try
    {
        json data = json::parse(jsonStr);
        json articleData = data["data"]["article"];

        // 基本字段
        lid = articleData.value("lid", "");
        title = articleData.value("title", "");
        category = articleData.value("category", 0);
        time = articleData.value("time", 0LL);

        // 作者信息
        if (articleData.contains("author") && !articleData["author"].is_null())
        {
            author_uid = articleData["author"].value("uid", 0);
            author_name = articleData["author"].value("name", "");
            author_avatar = articleData["author"].value("avatar", "");
        }

        // 统计数据
        upvote = articleData.value("upvote", 0);
        reply_count = articleData.value("replyCount", 0);
        favor_count = articleData.value("favorCount", 0);
        status = articleData.value("status", 0);

        // 对应的题解信息
        if (articleData.contains("solutionFor") && !articleData["solutionFor"].is_null())
        {
            solution_pid = articleData["solutionFor"].value("pid", "");
            solution_type = articleData["solutionFor"].value("type", "");
            solution_name = articleData["solutionFor"].value("name", "");
            solution_difficulty = articleData["solutionFor"].value("difficulty", 0);
        }

        promote_status = articleData.value("promoteStatus", 0);

        // 文章内容
        content = articleData.value("content", "");
        content_full = articleData.value("contentFull", false);

        if (articleData.contains("adminNote") && !articleData["adminNote"].is_null())
            admin_note = articleData["adminNote"].get<std::string>();
    }
    catch (const std::exception &e)
    {
        // JSON 解析失败，保留默认值
        std::fprintf(stderr, "Failed to parse JSON: %s\n", e.what());
    }
}

std::vector<std::string> Article::image_urls() const
{
    return image_util::extract_urls(content);
}

void Article::print()
{
    printf("lid: %s  title: %s\n", lid.c_str(), title.c_str());
    printf("category: %d  time: %lld\n", category, time);
    printf("author uid: %d  author name: %s\n\n", author_uid, author_name.c_str());

    printf("upvote: %d  replyCount: %d  favorCount: %d\n", upvote, reply_count, favor_count);
    printf("status: %d  promoteStatus: %d\n", status, promote_status);

    if(!solution_pid.empty())
        printf("solutionFor pid: %s  type: %s  name: %s  difficulty: %d\n\n",
               solution_pid.c_str(), solution_type.c_str(), solution_name.c_str(), solution_difficulty);

    printf("content:\n%s\n", content.c_str());
}
