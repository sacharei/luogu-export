// include/luogu-export/contents/article.h
#ifndef LUOGU_EXPORT_CONTENTS_ARTICLE_H
#define LUOGU_EXPORT_CONTENTS_ARTICLE_H

#include <string>
#include <vector>

namespace article
{
    struct Article
    {
    public:
        std::string lid, title;
        int category;
        long long time;
        int author_uid; std::string author_name, author_avatar;
        int upvote, reply_count, favor_count;
        int status;
        std::string solution_pid, solution_type, solution_name;
        int solution_difficulty;
        int promote_status;
        std::string content, admin_note;
        bool content_full;

        Article();
        Article(std::string html);

        // 扫描文章正文 markdown 中的图片链接，返回去重后的链接列表
        std::vector<std::string> image_urls() const;

        void print();
    };
}

#endif // LUOGU_EXPORT_CONTENTS_ARTICLE_H
