#include "Character.h"
#include <stdexcept>

namespace Game
{
    void ValidateNewCharacter(const NewCharacter& character)
    {
        if (character.name.empty() || character.name.size() >= 32)
            throw std::invalid_argument("名字必须为 1～31 个旧编码字节");
        bool trail = false;
        for (unsigned char byte : character.name)
        {
            if (byte <= 0x20 || byte == 0xff || (byte >= 0x7f && byte <= 0xa0))
                throw std::invalid_argument("名字包含服务端不允许的字符");
            if (trail)
            {
                if (!((byte >= 0x40 && byte <= 0x7e) || (byte >= 0xa1 && byte <= 0xfe)))
                    throw std::invalid_argument("名字编码无效");
                trail = false;
            }
            else if (byte >= 0xa1) trail = true;
        }
        if (trail) throw std::invalid_argument("名字编码不完整");
        if (character.model < 0 || character.model >= 12 || character.color < 0 || character.color >= 4 ||
            character.hometown < 0 || character.hometown >= 4)
            throw std::invalid_argument("外观或出生地无效");
        int total = 0;
        for (int point : character.points)
        {
            if (point < 0 || point > 20) throw std::invalid_argument("四围点数无效");
            total += point;
        }
        if (total != 20) throw std::invalid_argument("四围合计必须为 20");
        total = 0;
        int count = 0;
        for (int element : character.elements)
        {
            if (element < 0 || element > 10) throw std::invalid_argument("元素点数无效");
            total += element;
            if (element) ++count;
        }
        if (total != 10 || count > 2 || (character.elements[0] && character.elements[2]) ||
            (character.elements[1] && character.elements[3]))
            throw std::invalid_argument("元素组合无效");
    }
}
