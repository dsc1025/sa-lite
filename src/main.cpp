#include "LoginClient.h"
#include <conio.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
    std::string DisplayName(const std::string& name)
    {
        if (name.empty())
            return "";
        int length = MultiByteToWideChar(936, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
        if (length == 0)
            return "(名字无法显示)";
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(936, 0, name.data(), static_cast<int>(name.size()), wide.data(), length);
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
        std::string text(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, text.data(), bytes, nullptr, nullptr);
        return text;
    }

    void ShowPlayer(const LoginClient& client)
    {
        const auto& p = client.GetPlayerStatus();
        if (!p.received)
        {
            std::cout << "人物属性尚未收到。\n";
            return;
        }
        std::cout << "\n人物属性\n等级 " << p.level << "  转生 " << p.rebirth
            << "  HP " << p.hp << '/' << p.maxHp << "  MP " << p.mp << '/' << p.maxMp
            << "\n体力 " << p.vital << "  腕力 " << p.strength << "  耐力 " << p.toughness
            << "  速度 " << p.dexterity
            << "\n攻击 " << p.attack << "  防御 " << p.defence << "  敏捷 " << p.quick
            << "  魅力 " << p.charm << "  运气 " << p.luck << "  感知 " << p.perception
            << "\n地 " << p.earth << "  水 " << p.water << "  火 " << p.fire << "  风 " << p.wind
            << "\n经验 " << p.experience << "  下级经验 " << p.nextExperience
            << "  金币 " << p.gold << "  决斗积分 " << p.duelPoints << '\n';
    }

    void ShowPets(const LoginClient& client)
    {
        std::cout << "\n宠物\n";
        const auto& pets = client.GetPets();
        for (std::size_t i = 0; i < pets.size(); ++i)
        {
            const auto& p = pets[i];
            std::cout << "栏位 " << i + 1 << ": ";
            if (!p.received)
                std::cout << "尚未收到\n";
            else if (!p.present)
                std::cout << "空\n";
            else
                std::cout << DisplayName(p.nickname.empty() ? p.name : p.nickname)
                    << "（" << DisplayName(p.name) << "） 等级 " << p.level << "  转生 " << p.rebirth
                    << "\n  HP " << p.hp << '/' << p.maxHp << "  MP " << p.mp << '/' << p.maxMp
                    << "  攻击 " << p.attack << "  防御 " << p.defence << "  敏捷 " << p.quick
                    << "  忠诚 " << p.loyalty << "  感知 " << p.perception
                    << "\n  地 " << p.earth << "  水 " << p.water << "  火 " << p.fire << "  风 " << p.wind
                    << "  技能栏 " << p.skillSlots << "  经验 " << p.experience
                    << "  下级经验 " << p.nextExperience << '\n';
        }
    }

    void ShowItems(const LoginClient& client)
    {
        if (!client.HasItems())
        {
            std::cout << "道具列表尚未收到。\n";
            return;
        }
        const char* equipment[] = { "头部", "身体", "武器", "饰品1", "饰品2" };
        std::cout << "\n装备与背包\n";
        const auto& items = client.GetItems();
        bool found = false;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            if (!item.present)
                continue;
            found = true;
            if (i < 5)
                std::cout << equipment[i];
            else
                std::cout << "背包 " << i - 4;
            std::cout << ": " << DisplayName(item.name) << "  使用等级 " << item.level << '\n';
            if (!item.description.empty())
                std::cout << "  " << DisplayName(item.description) << '\n';
        }
        if (!found)
            std::cout << "没有装备或道具。\n";
    }

    bool ReadPassword(std::string& password)
    {
        for (;;)
        {
            int character = _getch();
            if (character == '\r')
            {
                std::cout << '\n';
                return true;
            }
            if (character == 27 || character == 3)
                return false;
            if (character == 0 || character == 224)
            {
                _getch();
                continue;
            }
            if (character == '\b' && !password.empty())
            {
                password.pop_back();
                std::cout << "\b \b" << std::flush;
            }
            else if (character >= 33 && character <= 126 && password.size() < 15)
            {
                password += static_cast<char>(character);
                std::cout << '*' << std::flush;
            }
        }
    }
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    std::string password;
    try
    {
        std::string address, portText, account;
        std::cout << "GMSV 服务器 IPv4 地址: " << std::flush;
        if (!std::getline(std::cin, address))
            return 0;
        std::cout << "端口 [9065]: " << std::flush;
        if (!std::getline(std::cin, portText))
            return 0;
        if (portText.empty())
            portText = "9065";
        if (portText.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("Invalid port");
        auto port = std::stoul(portText);
        if (port == 0 || port > 65535)
            throw std::invalid_argument("Port must be 1..65535");

        std::cout << "账号（1~15 个 ASCII 字符）: " << std::flush;
        if (!std::getline(std::cin, account))
            return 0;
        std::cout << "密码（Esc 取消）: " << std::flush;
        if (!ReadPassword(password))
        {
            SecureZeroMemory(password.data(), password.size());
            return 0;
        }
        LoginClient client;
        std::cout << "正在连接服务器…\n";
        client.Connect(address, static_cast<std::uint16_t>(port));
        std::cout << "已连接，正在登录…\n";
        auto result = client.Login(account, password);
        SecureZeroMemory(password.data(), password.size());
        password.clear();
        if (result == LoginClient::LoginResult::Rejected)
        {
            std::cerr << "登录被服务器拒绝，请检查账号、密码及账号服务状态。\n";
            return 1;
        }
        std::cout << "账号登录成功，正在获取角色列表…\n";
        auto characters = client.GetCharacters();
        if (characters.empty())
        {
            std::cout << "账号下没有角色，请先用旧客户端创建角色。\n";
            return 0;
        }
        for (std::size_t i = 0; i < characters.size(); ++i)
        {
            std::cout << i + 1 << ". " << DisplayName(characters[i].name);
            if (characters[i].level >= 0)
                std::cout << "  等级 " << characters[i].level;
            std::cout << '\n';
        }
        std::string input;
        std::size_t selected = 0;
        for (;;)
        {
            std::cout << "选择角色编号（0 退出）: " << std::flush;
            if (!std::getline(std::cin, input) || input == "0")
                return 0;
            if (!input.empty() && input.find_first_not_of("0123456789") == std::string::npos)
            {
                try
                {
                    auto number = std::stoul(input);
                    if (number >= 1 && number <= characters.size())
                    {
                        selected = number - 1;
                        break;
                    }
                }
                catch (const std::exception&) {}
            }
            std::cout << "请输入列表中的角色编号。\n";
        }
        std::cout << "正在进入游戏…\n";
        client.EnterGame(characters[selected]);
        std::cout << "已进入游戏：" << DisplayName(characters[selected].name)
            << "。正在接收人物、宠物、道具数据。\n"
            << "按 1 查看人物，2 查看宠物，3 查看道具，Enter 或 Esc 退出。\n";
        bool playerShown = false, petsShown = false, itemsShown = false;
        for (;;)
        {
            if (_kbhit())
            {
                int character = _getch();
                if (character == '\r' || character == 27)
                    break;
                if (character == '1')
                    ShowPlayer(client);
                else if (character == '2')
                    ShowPets(client);
                else if (character == '3')
                    ShowItems(client);
                if (character == 0 || character == 224)
                    _getch();
            }
            client.PollGame();
            if (!playerShown && client.GetPlayerStatus().received)
            {
                ShowPlayer(client);
                playerShown = true;
            }
            const auto& pets = client.GetPets();
            if (!petsShown && std::all_of(pets.begin(), pets.end(), [](const LoginClient::PetStatus& pet) { return pet.received; }))
            {
                ShowPets(client);
                petsShown = true;
            }
            if (!itemsShown && client.HasItems())
            {
                ShowItems(client);
                itemsShown = true;
            }
        }
        std::cout << "正在退出并等待服务器保存角色…\n";
        client.LeaveGame();
        std::cout << "角色已退出。\n";
    }
    catch (const std::exception& error)
    {
        SecureZeroMemory(password.data(), password.size());
        std::cerr << "连接或游戏会话失败: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
