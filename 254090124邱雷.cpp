// ============================================================
// 记事本系统 · C++ 控制台版
// 标准库实现，无第三方依赖，C++11 及以上即可编译
//   编译: g++ -O2 -std=c++17 记事本.cpp -o 记事本
//   运行: ./记事本
// 数据保存在与程序同目录的 notes.dat（JSON 风格自定义文本格式）
// ============================================================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <algorithm>
#include <cctype>

using namespace std;

// ---------- 基础工具 ----------
static string trim(const string& s){
    size_t b = s.find_first_not_of(" \t\r\n");
    if(b == string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
static string toLower(string s){
    for(auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static string fmtTime(long long t){
    time_t tt = (time_t)t; struct tm tm;
    struct tm* ptm = localtime(&tt);
    if(ptm) tm = *ptm;
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}
static vector<string> split(const string& s, char d){
    vector<string> out; string cur;
    for(char c : s){ if(c == d){ out.push_back(cur); cur.clear(); } else cur += c; }
    if(!cur.empty()) out.push_back(cur);
    return out;
}
static string join(const vector<string>& v, const string& sep){
    string out;
    for(size_t i = 0; i < v.size(); ++i){ if(i) out += sep; out += v[i]; }
    return out;
}
// 安全读取一行：兼容 cin>>num 后残留的换行
static string readLine(const string& prompt){
    if(!prompt.empty()) cout << prompt;
    string s; if(!getline(cin, s)) s = "";
    return s;
}

// ---------- 数据模型 ----------
struct Note{
    int id = 0;
    string title, content, category;
    vector<string> tags;
    long long createdAt = 0, updatedAt = 0;
    bool pinned = false, fav = false, deleted = false;
};

struct App{
    vector<Note> notes;
    vector<string> categories;
    string dataFile = "notes.dat";
    int nextId = 1;
    // 当前视图状态
    int view = 0;            // 0=全部 1=收藏 2=回收站 3=分类
    string viewCat;         // 分类视图时的分类名
    string search;          // 搜索关键词
    bool dirty = false;

    // ---------- 序列化 ----------
    void save(){
        ofstream f(dataFile);
        if(!f){ cout << "!! 无法写入 " << dataFile << endl; return; }
        f << "[CATS]\n" << categories.size() << "\n";
        for(auto& c : categories) f << c << "\n";
        f << "[NOTES]\n" << notes.size() << "\n";
        for(auto& n : notes){
            f << "#N\n" << n.id << "\n" << n.title << "\n" << n.category << "\n";
            f << (n.pinned?1:0) << " " << (n.fav?1:0) << " " << (n.deleted?1:0) << "\n";
            f << n.createdAt << "\n" << n.updatedAt << "\n";
            f << n.tags.size() << "\n";
            for(auto& t : n.tags) f << t << "\n";
            f << n.content.size() << "\n" << n.content << "\n";
        }
        f.close();
        dirty = false;
    }
    void load(){
        ifstream f(dataFile);
        if(!f) return;
        string line;
        auto readCount = [&]() -> int { getline(f, line); return atoi(trim(line).c_str()); };

        // [CATS]
        getline(f, line);                       // 吃掉 [CATS]
        int nc = readCount();
        categories.clear();
        for(int i = 0; i < nc; ++i){ getline(f, line); categories.push_back(line); }

        // [NOTES]
        getline(f, line);                       // 吃掉 [NOTES]
        int nn = readCount();
        notes.clear(); nextId = 1;
        for(int i = 0; i < nn; ++i){
            getline(f, line);                   // #N
            Note n;
            getline(f, line); n.id = atoi(line.c_str());
            getline(f, line); n.title = line;
            getline(f, line); n.category = line;
            { int a,b,c; f >> a >> b >> c; f.ignore(); n.pinned = a; n.fav = b; n.deleted = c; }
            { long long a,b; f >> a >> b; f.ignore(); n.createdAt = a; n.updatedAt = b; }
            { int nt; getline(f, line); nt = atoi(line.c_str());
              for(int k = 0; k < nt; ++k){ getline(f, line); n.tags.push_back(line); } }
            { int cl; getline(f, line); cl = atoi(line.c_str());
              string content; content.reserve(cl + 1);
              while((int)content.size() < cl){ getline(f, line); content += line; content += '\n'; }
              if(!content.empty() && content.back() == '\n') content.pop_back();
              n.content = content; }
            if(n.id >= nextId) nextId = n.id + 1;
            notes.push_back(n);
        }
        f.close();
    }

    // ---------- 查询 ----------
    vector<Note*> visible(){
        vector<Note*> out;
        for(auto& n : notes){
            bool ok = false;
            if(view == 2) ok = n.deleted;
            else if(n.deleted) ok = false;
            else if(view == 1) ok = n.fav;
            else if(view == 3) ok = (n.category == viewCat);
            else ok = true;
            if(!ok) continue;
            if(!search.empty()){
                string kw = toLower(search);
                bool hit = toLower(n.title).find(kw) != string::npos ||
                           toLower(n.content).find(kw) != string::npos;
                for(auto& t : n.tags) if(toLower(t).find(kw) != string::npos) hit = true;
                if(!hit) continue;
            }
            out.push_back(&n);
        }
        sort(out.begin(), out.end(), [](Note* a, Note* b){
            if(a->pinned != b->pinned) return a->pinned;
            return a->updatedAt > b->updatedAt;
        });
        return out;
    }
    Note* findById(int id){
        for(auto& n : notes) if(n.id == id) return &n;
        return nullptr;
    }

    // ---------- 辅助 ----------
    void touch(Note& n){ n.updatedAt = time(nullptr); dirty = true; }
    string catLabel(){ switch(view){ case 0: return "全部笔记"; case 1: return "收藏"; case 2: return "回收站"; case 3: return "分类 · " + viewCat; } return ""; }
    bool inTrashView() const { return view == 2; }
};

// ---------- 渲染 ----------
static void renderList(App& app){
    auto v = app.visible();
    cout << "\n════════════════════════════════════════\n";
    cout << "  " << app.catLabel()
         << (app.search.empty() ? "" : "  搜索: " + app.search)
         << "  共 " << v.size() << " 篇\n";
    cout << "════════════════════════════════════════\n";
    if(v.empty()){ cout << "  (空)\n"; return; }
    int idx = 1;
    for(auto* n : v){
        string snip = n->content;
        for(auto& c : snip) if(c == '\n' || c == '\r') c = ' ';
        if(snip.size() > 42) snip = snip.substr(0, 42) + "…";
        if(snip.empty()) snip = "(空白)";
        cout << "  [" << setw(3) << idx << "] "
             << (n->pinned ? "★ " : "  ")
             << (n->fav ? "♡ " : "  ")
             << (n->title.empty() ? "(无标题)" : n->title)
             << "   #" << n->category;
        if(!n->tags.empty()) cout << "  " << join(n->tags, ", ");
        cout << "\n        " << snip
             << "  — " << fmtTime(n->updatedAt) << "\n";
        ++idx;
    }
}

static void renderNote(App& app, Note& n){
    cout << "\n────────────────────────────────────────\n";
    cout << "  标题: " << (n.title.empty() ? "(无标题)" : n.title) << "\n";
    cout << "  分类: " << (n.category.empty() ? "未分类" : n.category) << "\n";
    cout << "  标签: " << (n.tags.empty() ? "无" : join(n.tags, ", ")) << "\n";
    cout << "  标记: " << (n.pinned ? "置顶 " : "") << (n.fav ? "收藏 " : "") << (n.deleted ? "已删除" : "") << "\n";
    cout << "  创建: " << fmtTime(n.createdAt) << "   更新: " << fmtTime(n.updatedAt) << "\n";
    cout << "────────────────────────────────────────\n";
    cout << "  " << n.content << "\n";
}

// ---------- 业务操作 ----------
static void newNote(App& app){
    Note n;
    n.id = app.nextId++;
    n.title = readLine("  标题(可留空): ");
    n.content = readLine("  内容(可留空): ");
    string cat = readLine("  分类(回车不设): ");
    if(!cat.empty() && find(app.categories.begin(), app.categories.end(), cat) == app.categories.end()){
        app.categories.push_back(cat);   // 允许临时新建分类
    }
    n.category = cat;
    string tags = readLine("  标签(逗号分隔): ");
    for(auto& t : split(tags, ',')){ string x = trim(t); if(!x.empty()) n.tags.push_back(x); }
    time_t now = time(nullptr);
    n.createdAt = n.updatedAt = now;
    app.notes.push_back(n);
    app.dirty = true;
    cout << "  ✓ 已创建笔记 #" << n.id << "\n";
}

static void openNote(App& app){
    auto v = app.visible();
    if(v.empty()){ cout << "  当前没有可打开的笔记。\n"; return; }
    string in = readLine("  输入编号打开(回车返回): ");
    if(in.empty()) return;
    int idx = atoi(in.c_str());
    if(idx < 1 || idx > (int)v.size()){ cout << "  !! 无效编号\n"; return; }
    Note* n = v[idx - 1];
    bool inTrash = n->deleted;

    renderNote(app, *n);
    cout << "\n  操作: [e]编辑  [t]置顶/取消  [f]收藏/取消  [d]"
         << (inTrash ? "彻底删除" : "移入回收站");
    if(inTrash) cout << "  [r]恢复";
    cout << "  [q]返回\n";
    string op = readLine("  > ");
    op = trim(toLower(op));
    if(op == "e" && !inTrash){
        string t = readLine("  新标题(留空不改): ");
        if(!t.empty()) n->title = t;
        cout << "  新内容(输入 #END 结束; 直接 #END 表示不改):\n";
        { string line, buf;
          while(getline(cin, line)){ if(trim(line) == "#END") break; buf += line + "\n"; }
          if(!buf.empty()) buf.pop_back();
          if(!buf.empty()) n->content = buf; }
        string cat = readLine("  新分类(留空不改): ");
        if(!cat.empty()){
            if(find(app.categories.begin(), app.categories.end(), cat) == app.categories.end())
                app.categories.push_back(cat);
            n->category = cat;
        }
        string tags = readLine("  新标签(逗号分隔, 留空不改): ");
        if(!tags.empty()){
            n->tags.clear();
            for(auto& t : split(tags, ',')){ string x = trim(t); if(!x.empty()) n->tags.push_back(x); }
        }
        app.touch(*n);
        cout << "  ✓ 已保存\n";
    }
    else if(op == "t"){ n->pinned = !n->pinned; app.touch(*n); cout << (n->pinned ? "  ✓ 已置顶\n" : "  ✓ 已取消置顶\n"); }
    else if(op == "f"){ n->fav = !n->fav; app.touch(*n); cout << (n->fav ? "  ✓ 已收藏\n" : "  ✓ 已取消收藏\n"); }
    else if(op == "d"){
        if(!inTrash){
            n->deleted = true; n->pinned = false; app.touch(*n);
            cout << "  ✓ 已移入回收站\n";
        } else {
            for(auto it = app.notes.begin(); it != app.notes.end(); ++it)
                if(it->id == n->id){ app.notes.erase(it); break; }
            app.dirty = true;
            cout << "  ✓ 已彻底删除\n";
        }
    }
    else if(op == "r" && inTrash){
        n->deleted = false; app.touch(*n);
        cout << "  ✓ 已恢复\n";
    }
    else if(op != "q"){ cout << "  无效操作\n"; }
}

static void searchNotes(App& app){
    string kw = readLine("  输入关键词: ");
    app.search = trim(kw);
    cout << "  (按回车返回列表)\n";
    readLine("");
    app.search.clear();
}

static void manageCategories(App& app){
    while(true){
        cout << "\n────── 分类管理 ──────\n";
        if(app.categories.empty()) cout << "  (暂无分类)\n";
        for(size_t i = 0; i < app.categories.size(); ++i){
            int cnt = 0; for(auto& n : app.notes) if(!n.deleted && n.category == app.categories[i]) ++cnt;
            cout << "  [" << (i+1) << "] " << app.categories[i] << "  (" << cnt << " 篇)\n";
        }
        cout << "  操作: [a]新增  [d N]删除第N个  [q]返回\n  > ";
        string op; getline(cin, op); op = trim(toLower(op));
        if(op == "q" || op.empty()) return;
        if(op == "a"){
            string name = readLine("  分类名: "); name = trim(name);
            if(name.empty()){ cout << "  !! 名称不能为空\n"; continue; }
            if(find(app.categories.begin(), app.categories.end(), name) != app.categories.end())
                { cout << "  !! 已存在\n"; continue; }
            app.categories.push_back(name); app.dirty = true;
            cout << "  ✓ 已添加\n";
        }
        else if(op.rfind("d ", 0) == 0 || op == "d"){
            int idx = (op == "d") ? atoi(readLine("  删除第几个: ").c_str()) : atoi(op.substr(2).c_str());
            if(idx < 1 || idx > (int)app.categories.size()){ cout << "  !! 无效编号\n"; continue; }
            string rm = app.categories[idx - 1];
            for(auto& n : app.notes) if(n.category == rm) n.category = "";
            app.categories.erase(app.categories.begin() + (idx - 1));
            if(app.viewCat == rm) app.viewCat = "";
            app.dirty = true;
            cout << "  ✓ 已删除分类 " << rm << "\n";
        }
        else cout << "  !! 无效操作\n";
    }
}

static void exportData(App& app){
    string file = readLine("  导出文件名(默认 backup.json): ");
    if(trim(file).empty()) file = "backup.json";
    ofstream f(file);
    if(!f){ cout << "  !! 无法写入\n"; return; }
    f << "{\n  \"notes\": " << app.notes.size() << ",\n  \"categories\": [";
    for(size_t i = 0; i < app.categories.size(); ++i){ if(i) f << ", "; f << "\"" << app.categories[i] << "\""; }
    f << "]\n}\n";
    f.close();
    cout << "  ✓ 已导出(仅元信息摘要)到 " << file << "\n";
}

// ============================================================
// 登录系统：SHA-256 加盐哈希 + users.dat 账号存储 + 每账号独立数据文件
// ============================================================
// ---- SHA-256（紧凑标准实现，可按官方向量自验）----
static string sha256hex(const string& data){
    static const uint32_t K[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t H[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto R=[](uint32_t x,int n){return (x>>n)|(x<<(32-n));};
    uint64_t bitlen=data.size()*8;
    string m=data; m+=(char)0x80;
    while(m.size()%64!=56) m+=(char)0;
    for(int i=7;i>=0;i--) m+=(char)((bitlen>>(i*8))&0xff);
    for(size_t off=0;off<m.size();off+=64){
        uint32_t w[64];
        for(int i=0;i<16;i++)
            w[i]=((uint32_t)(uint8_t)m[off+i*4]<<24)|((uint32_t)(uint8_t)m[off+i*4+1]<<16)
                |((uint32_t)(uint8_t)m[off+i*4+2]<<8)|(uint32_t)(uint8_t)m[off+i*4+3];
        for(int i=16;i<64;i++){
            uint32_t s0=R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3);
            uint32_t s1=R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        uint32_t a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
        for(int i=0;i<64;i++){
            uint32_t S1=R(e,6)^R(e,11)^R(e,25);
            uint32_t ch=(e&f)^(~e&g);
            uint32_t t1=h+S1+ch+K[i]+w[i];
            uint32_t S0=R(a,2)^R(a,13)^R(a,22);
            uint32_t maj=(a&b)^(a&c)^(b&c);
            uint32_t t2=S0+maj;
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        H[0]+=a;H[1]+=b;H[2]+=c;H[3]+=d;H[4]+=e;H[5]+=f;H[6]+=g;H[7]+=h;
    }
    static const char* hexd="0123456789abcdef";
    string out;
    for(int i=0;i<8;i++) for(int j=7;j>=0;j--) out+=hexd[(H[i]>>(j*4))&0xf];
    return out;
}

// ---- 用户存储 ----
struct User{ string name, salt, hash; };
static const string USER_FILE="users.dat";

static vector<User> loadUsers(){
    vector<User> us; ifstream f(USER_FILE);
    if(!f) return us;
    string line; getline(f,line);          // [USERS]
    getline(f,line); int n=atoi(trim(line).c_str());
    for(int i=0;i<n&&getline(f,line);++i){
        User u; u.name=line;
        getline(f,line); u.salt=line;
        getline(f,line); u.hash=line;
        us.push_back(u);
    }
    return us;
}
static void saveUsers(const vector<User>& us){
    ofstream f(USER_FILE);
    f<<"[USERS]\n"<<us.size()<<"\n";
    for(auto& u:us) f<<u.name<<"\n"<<u.salt<<"\n"<<u.hash<<"\n";
}
static User* findUser(vector<User>& us,const string& name){
    for(auto& u:us) if(u.name==name) return &u;
    return nullptr;
}
static string genSalt(){
    srand((unsigned)time(nullptr)+rand());
    static const char* cs="0123456789abcdef";
    string s; for(int i=0;i<16;++i) s+=cs[rand()%16]; return s;
}
// 注册：返回是否成功
static bool doRegister(vector<User>& users,string& current){
    string name=trim(readLine("  用户名(字母/数字/下划线): "));
    if(name.empty()){ cout<<"  !! 用户名不能为空\n"; return false; }
    for(char c:name) if(!isalnum((unsigned char)c)&&c!='_'&&c!='-'){
        cout<<"  !! 用户名只能包含字母、数字、下划线、短横线\n"; return false; }
    if(findUser(users,name)){ cout<<"  !! 该账号已存在\n"; return false; }
    string p1=readLine("  密码(至少4位): ");
    if(p1.size()<4){ cout<<"  !! 密码至少 4 位\n"; return false; }
    string p2=readLine("  确认密码: ");
    if(p1!=p2){ cout<<"  !! 两次密码不一致\n"; return false; }
    User u; u.name=name; u.salt=genSalt(); u.hash=sha256hex(p1+u.salt);
    users.push_back(u); saveUsers(users); current=name;
    cout<<"  ✓ 账号 "<<name<<" 注册成功\n";
    return true;
}
// 登录：返回是否成功
static bool doLogin(vector<User>& users,string& current){
    string name=trim(readLine("  用户名: "));
    User* u=findUser(users,name);
    if(!u){ cout<<"  !! 账号不存在\n"; return false; }
    string p=readLine("  密码: ");
    if(sha256hex(p+u->salt)!=u->hash){ cout<<"  !! 密码错误\n"; return false; }
    current=name; cout<<"  ✓ 登录成功，欢迎 "<<name<<"\n";
    return true;
}

int main(){
    cout << "\n   ╔══════════════════════════╗\n";
    cout << "   ║     记事本系统 (C++)      ║\n";
    cout << "   ║      多账号 · 独立数据     ║\n";
    cout << "   ╚══════════════════════════╝\n";

    /* ---- 登录门禁 ---- */
    vector<User> users=loadUsers();
    string current;
    if(users.empty()){
        cout << "  首次使用，请先注册一个账号。\n";
        while(!doRegister(users,current)){}
    }else{
        cout << "  已有账号 " << users.size() << " 个。\n";
        while(true){
            cout << "\n  [1] 登录  [2] 注册新账号  [0] 退出\n  > ";
            string op; getline(cin,op); op=trim(op);
            if(op=="1"){ if(doLogin(users,current)) break; }
            else if(op=="2"){ if(doRegister(users,current)) break; }
            else if(op=="0"){ cout<<"  再见！\n"; return 0; }
            else cout<<"  !! 无效选项\n";
        }
    }

    /* ---- 进入当前账号的记事本 ---- */
    App app;
    app.dataFile = "notes_" + current + ".dat";
    app.load();
    if(app.categories.empty()){
        app.categories.push_back("工作"); app.categories.push_back("生活");
        app.dirty = true;
    }

    cout << "   当前用户: " << current << "\n";
    cout << "   数据文件: " << app.dataFile << "\n";

    while(true){
        renderList(app);
        cout << "\n  ── 菜单 ────────────────────\n";
        cout << "  [1] 新建笔记   [2] 打开笔记   [3] 搜索\n";
        cout << "  [4] 分类管理   [5] 视图切换   [6] 导出备份\n";
        cout << "  [0] 保存并退出\n  > ";
        string op; getline(cin, op); op = trim(op);
        if(op == "0") break;
        else if(op == "1") newNote(app);
        else if(op == "2") openNote(app);
        else if(op == "3") searchNotes(app);
        else if(op == "4") manageCategories(app);
        else if(op == "5"){
            cout << "  [0]全部 [1]收藏 [2]回收站 [3]按分类\n  > ";
            string v; getline(cin, v); v = trim(v);
            int nv = atoi(v.c_str());
            if(nv == 3){
                cout << "  分类: "; string c; getline(cin, c); app.viewCat = trim(c);
            }
            if(nv >= 0 && nv <= 3) app.view = nv;
            app.search.clear();
        }
        else if(op == "6") exportData(app);
        else cout << "  !! 无效选项\n";
    }

    app.save();
    cout << "  ✓ 已保存，再见！\n";
    return 0;
}
//（注：内容由AI生成）
