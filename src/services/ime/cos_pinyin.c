/**
 * @file eos_pinyin.c
 * @brief Compact pinyin dictionary (see eos_pinyin.h).
 *
 * The dictionary below is a curated, compact set of common syllables and a
 * handful of frequent words. It is intentionally small to fit embedded
 * constraints; it can be extended by appending entries (keep py lowercase).
 * Multiple entries may share the same pinyin to offer character choices.
 */

#include "eos_pinyin.h"

#include <string.h>

/* The dictionary. Order is irrelevant; lookup is linear. */
static const eos_pinyin_entry_t s_dict[] = {
    /* ---- single syllables (common characters) ---- */
    {"a",      "啊"}, {"ai",     "爱"}, {"an",     "安"}, {"ang",    "昂"},
    {"ao",     "奥"}, {"ba",     "八"}, {"bai",    "白"}, {"ban",    "办"},
    {"bang",   "帮"}, {"bao",    "包"}, {"bei",    "北"}, {"ben",    "本"},
    {"beng",   "崩"}, {"bi",     "比"}, {"bian",   "边"}, {"biao",   "表"},
    {"bie",    "别"}, {"bin",    "宾"}, {"bing",   "病"}, {"bo",     "波"},
    {"bu",     "不"}, {"ca",     "擦"}, {"cai",    "才"}, {"can",    "参"},
    {"cang",   "藏"}, {"cao",    "草"}, {"ce",     "册"}, {"ceng",   "层"},
    {"cha",    "茶"}, {"chai",   "柴"}, {"chan",   "产"}, {"chang",  "长"},
    {"chao",   "超"}, {"che",    "车"}, {"chen",   "陈"}, {"cheng",  "成"},
    {"chi",    "吃"}, {"chong",  "充"}, {"chou",   "抽"}, {"chu",    "出"},
    {"chuan",  "穿"}, {"chuang", "床"}, {"chun",   "春"}, {"ci",     "次"},
    {"cong",   "从"}, {"cu",     "促"}, {"cuan",   "窜"}, {"cui",    "催"},
    {"cun",    "村"}, {"cuo",    "错"}, {"da",     "大"}, {"dai",    "带"},
    {"dan",    "但"}, {"dang",   "当"}, {"dao",    "到"}, {"de",     "的"},
    {"deng",   "等"}, {"di",     "地"}, {"dian",   "电"}, {"diao",   "吊"},
    {"die",    "叠"}, {"ding",   "定"}, {"diu",    "丢"}, {"dong",   "东"},
    {"dou",    "都"}, {"du",     "度"}, {"duan",   "段"}, {"dui",    "对"},
    {"dun",    "顿"}, {"duo",    "多"}, {"e",      "饿"}, {"en",     "恩"},
    {"er",     "二"}, {"fa",     "发"}, {"fan",    "反"}, {"fang",   "方"},
    {"fei",    "飞"}, {"fen",    "分"}, {"feng",   "风"}, {"fu",     "服"},
    {"ga",     "嘎"}, {"gai",    "改"}, {"gan",    "干"}, {"gang",   "刚"},
    {"gao",    "高"}, {"ge",     "个"}, {"gen",    "跟"}, {"geng",   "更"},
    {"gong",   "工"}, {"gou",    "够"}, {"gu",     "古"}, {"gua",    "瓜"},
    {"guan",   "关"}, {"guang",  "光"}, {"gui",    "归"}, {"guo",    "国"},
    {"ha",     "哈"}, {"hai",    "海"}, {"han",    "汉"}, {"hang",   "航"},
    {"hao",    "好"}, {"he",     "和"}, {"hen",    "很"}, {"heng",   "横"},
    {"hong",   "红"}, {"hou",    "后"}, {"hu",     "户"}, {"hua",    "华"},
    {"huai",   "怀"}, {"huan",   "欢"}, {"huang",  "黄"}, {"hui",    "会"},
    {"hun",    "混"}, {"huo",    "活"}, {"ji",     "机"}, {"jia",    "家"},
    {"jian",   "间"}, {"jiang",  "江"}, {"jiao",   "交"}, {"jie",    "节"},
    {"jin",    "今"}, {"jing",   "经"}, {"jiong",  "炯"}, {"jiu",    "九"},
    {"ju",     "局"}, {"juan",   "卷"}, {"jue",    "决"}, {"jun",    "军"},
    {"ka",     "卡"}, {"kai",    "开"}, {"kan",    "看"}, {"kang",   "康"},
    {"kao",    "考"}, {"ke",     "可"}, {"ken",    "肯"}, {"keng",   "坑"},
    {"kong",   "空"}, {"kou",    "口"}, {"ku",     "苦"}, {"kua",    "跨"},
    {"kuai",   "快"}, {"kuan",   "宽"}, {"kuang",  "狂"}, {"kui",    "亏"},
    {"kun",    "困"}, {"kuo",    "阔"}, {"la",     "拉"}, {"lai",    "来"},
    {"lan",    "蓝"}, {"lang",   "狼"}, {"lao",    "老"}, {"le",     "了"},
    {"lei",    "类"}, {"leng",   "冷"}, {"li",     "里"}, {"lian",   "连"},
    {"liang",  "亮"}, {"liao",   "疗"}, {"lin",    "林"}, {"ling",   "令"},
    {"liu",    "六"}, {"long",   "龙"}, {"lou",    "楼"}, {"lu",     "路"},
    {"luan",   "乱"}, {"lue",    "略"}, {"lun",    "论"}, {"luo",    "落"},
    {"ma",     "马"}, {"mai",    "买"}, {"man",    "满"}, {"mang",   "忙"},
    {"mao",    "毛"}, {"me",     "么"}, {"mei",    "美"}, {"men",    "门"},
    {"meng",   "梦"}, {"mi",     "米"}, {"mian",   "面"}, {"miao",   "苗"},
    {"min",    "民"}, {"ming",   "明"}, {"miu",    "谬"}, {"mo",     "末"},
    {"mou",    "某"}, {"mu",     "母"}, {"na",     "那"}, {"nai",    "奶"},
    {"nan",    "南"}, {"nang",   "囊"}, {"nao",    "脑"}, {"ne",     "呢"},
    {"nei",    "内"}, {"nen",    "嫩"}, {"neng",   "能"}, {"ni",     "你"},
    {"nian",   "年"}, {"niang",  "娘"}, {"niao",   "鸟"}, {"nie",    "捏"},
    {"nin",    "您"}, {"ning",   "宁"}, {"niu",    "牛"}, {"nong",   "农"},
    {"nu",     "努"}, {"nuan",   "暖"}, {"nue",    "虐"}, {"nuo",    "诺"},
    {"o",      "哦"}, {"ou",     "欧"}, {"pa",     "怕"}, {"pai",    "排"},
    {"pan",    "盘"}, {"pang",   "旁"}, {"pao",    "跑"}, {"pei",     "配"},
    {"pen",    "盆"}, {"peng",   "朋"}, {"pi",     "皮"}, {"pian",   "片"},
    {"piao",   "票"}, {"pie",    "撇"}, {"pin",    "品"}, {"ping",   "平"},
    {"po",     "破"}, {"pu",     "普"}, {"qi",     "七"}, {"qia",    "恰"},
    {"qian",   "前"}, {"qiang",  "强"}, {"qiao",   "桥"}, {"qie",    "切"},
    {"qin",    "亲"}, {"qing",   "请"}, {"qiong",  "穷"}, {"qiu",    "求"},
    {"qu",     "去"}, {"quan",   "全"}, {"que",    "缺"}, {"qun",    "群"},
    {"ran",    "然"}, {"rang",   "让"}, {"rao",    "绕"}, {"re",     "热"},
    {"ren",    "人"}, {"reng",   "仍"}, {"ri",     "日"}, {"rong",   "容"},
    {"rou",    "柔"}, {"ru",     "如"}, {"ruan",   "软"}, {"rui",    "瑞"},
    {"run",    "润"}, {"ruo",    "弱"}, {"sa",     "撒"}, {"sai",    "赛"},
    {"san",    "三"}, {"sang",   "桑"}, {"sao",    "扫"}, {"se",     "色"},
    {"sen",    "森"}, {"sha",    "沙"}, {"shai",   "晒"}, {"shan",   "山"},
    {"shang",  "上"}, {"shao",   "少"}, {"she",    "社"}, {"shen",   "身"},
    {"sheng",  "生"}, {"shi",    "是"}, {"shou",   "手"}, {"shu",    "书"},
    {"shua",   "刷"}, {"shuai",  "摔"}, {"shuan",  "栓"}, {"shuang", "双"},
    {"shui",   "水"}, {"shun",   "顺"}, {"shuo",   "说"}, {"si",     "四"},
    {"song",   "送"}, {"sou",    "搜"}, {"su",     "速"}, {"suan",   "算"},
    {"sui",    "岁"}, {"sun",    "孙"}, {"suo",    "所"}, {"ta",     "他"},
    {"tai",    "台"}, {"tan",    "谈"}, {"tang",   "糖"}, {"tao",    "套"},
    {"te",     "特"}, {"teng",   "腾"}, {"ti",     "体"}, {"tian",   "天"},
    {"tiao",   "条"}, {"tie",    "贴"}, {"ting",   "听"}, {"tong",   "同"},
    {"tou",    "头"}, {"tu",     "图"}, {"tuan",   "团"}, {"tui",    "推"},
    {"tun",    "吞"}, {"tuo",    "托"}, {"wa",     "挖"}, {"wai",    "外"},
    {"wan",    "万"}, {"wang",   "王"}, {"wei",    "为"}, {"wen",    "文"},
    {"weng",   "翁"}, {"wo",     "我"}, {"wu",     "五"}, {"xi",     "西"},
    {"xia",    "下"}, {"xian",   "现"}, {"xiang",  "想"}, {"xiao",   "小"},
    {"xie",    "写"}, {"xin",    "新"}, {"xing",   "行"}, {"xiong",  "兄"},
    {"xiu",    "休"}, {"xu",     "许"}, {"xuan",   "选"}, {"xue",    "学"},
    {"xun",    "寻"}, {"ya",     "呀"}, {"yan",    "眼"}, {"yang",   "样"},
    {"yao",    "要"}, {"ye",     "也"}, {"yi",     "一"}, {"yin",    "音"},
    {"ying",   "应"}, {"yo",     "哟"}, {"yong",   "用"}, {"you",    "有"},
    {"yu",     "玉"}, {"yuan",   "元"}, {"yue",    "月"}, {"yun",    "云"},
    {"za",     "杂"}, {"zai",    "在"}, {"zan",    "赞"}, {"zang",   "藏"},
    {"zao",    "早"}, {"ze",     "则"}, {"zei",    "贼"}, {"zen",    "怎"},
    {"zeng",   "增"}, {"zha",    "扎"}, {"zhai",   "摘"}, {"zhan",   "站"},
    {"zhang",  "张"}, {"zhao",   "找"}, {"zhe",    "这"}, {"zhen",   "真"},
    {"zheng",  "正"}, {"zhi",    "只"}, {"zhong",  "中"}, {"zhou",   "周"},
    {"zhu",    "主"}, {"zhua",   "抓"}, {"zhuai",  "拽"}, {"zhuan",  "转"},
    {"zhuang", "装"}, {"zhui",   "追"}, {"zhun",   "准"}, {"zhuo",   "桌"},
    {"zi",     "字"}, {"zong",   "总"}, {"zou",    "走"}, {"zu",     "组"},
    {"zuan",   "钻"}, {"zui",    "最"}, {"zun",    "尊"}, {"zuo",    "左"},

    /* ---- frequent words (longer pinyin) ---- */
    {"nihao",    "你好"}, {"woai",     "我爱"}, {"zhongguo", "中国"},
    {"diannao",  "电脑"}, {"shuru",    "输入"}, {"xitong",   "系统"},
    {"jianpan",  "键盘"}, {"zhongwen", "中文"}, {"yingwen",  "英文"},
    {"shouji",   "手机"}, {"dianhua",  "电话"}, {"xinxi",    "信息"},
    {"wangluo",  "网络"}, {"chengxu",  "程序"}, {"kaifa",    "开发"},
    {"gongneng", "功能"}, {"shezhi",   "设置"}, {"yingyong", "应用"},
    {"chenggong","成功"}, {"shibai",   "失败"}, {"kaishi",   "开始"},
    {"jieshu",   "结束"}, {"zhongwen", "中文"}, {"qingchu",  "清楚"},
    {"fanhui",   "返回"}, {"queRen",   "确认"},
};

static int s_dict_size = (int)(sizeof(s_dict) / sizeof(s_dict[0]));

int eos_pinyin_dict_size(void)
{
    return s_dict_size;
}

int eos_pinyin_lookup(const char *py, const char **out, int max_out, int *out_count)
{
    int total = 0;
    if (out_count)
        *out_count = 0;
    if (!py)
        return 0;

    /* case-insensitive compare against the lowercase dictionary */
    for (int i = 0; i < s_dict_size; i++)
    {
        if (strcasecmp(s_dict[i].py, py) == 0)
        {
            if (out && total < max_out)
                out[total] = s_dict[i].han;
            total++;
            if (out_count)
                *out_count = total;
        }
    }
    return total;
}
