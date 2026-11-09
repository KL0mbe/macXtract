#include <Carbon/Carbon.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#define VERSION "macXtract 1.0.0"
static const UInt16 illegalChars[3] = {'"', '<', '&'};
static int isStrip = 0;

void intToChar(char output[256], int outputSize, UInt32 c)
{
    if (isStrip && c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D)
        return;
    if (c >= 0x20 && c <= 0x7E)
    {
        int found = 0;
        for (int i = 0; i < 3; i++)
            if (c == illegalChars[i])
                found = 1;
        if (!found)
        {
            snprintf(output + strlen(output), outputSize - strlen(output), "%c", c);
            return;
        }
    }
    snprintf(output + strlen(output), outputSize - strlen(output), "&#x%04x;", c);
}

void checkCharType(UCKeyCharSeq value, const UCKeySequenceDataIndex *sequenceTable, int hasSequenceTable, char *output, int outputSize)
{
    if (value == 0xffff || value == 0xfffe)
        return;

    snprintf(output + strlen(output), outputSize - strlen(output), "output=\"");
    if ((value & 0xC000) == 0x8000 && hasSequenceTable)
    {
        int l = value & 0x3FFF;
        if (l < sequenceTable->charSequenceCount)
        {
            const UInt16 *charSequenceOffsets = sequenceTable->charSequenceOffsets;
            const UniChar *chars = (const UniChar *)((const UInt8 *)sequenceTable + charSequenceOffsets[l]);
            int length = (charSequenceOffsets[l + 1] - charSequenceOffsets[l]) / 2;
            for (int c = 0; c < length; c++)
                if (chars[c] >= 0xD800 && chars[c] <= 0xDBFF && c + 1 < length && chars[c + 1] >= 0xDC00 && chars[c + 1] <= 0xDFFF)
                {
                    intToChar(output, outputSize, 0x10000 + ((chars[c] - 0xD800) << 10) + (chars[c + 1] - 0xDC00));
                    c++;
                }
                else
                    intToChar(output, outputSize, chars[c]);
            strcat(output, "\"");
            return;
        }
    }
    intToChar(output, outputSize, value);
    strcat(output, "\"");
}

// djb2 hash for Unique ID using layout name
int hashString(const char *s)
{
    unsigned long hash = 5381;
    while (*s)
        hash = hash * 33 + (unsigned char)*s++;
    hash = hash % 32767;
    return -(int)(hash + 1);
}

int main(int argc, char *argv[])
{
    int noComment = 0;
    const char *outputDir = ".";
    static const char *modifiersEnum[8] = {"command", "shift", "caps", "option",
                                           "control", "rightShift", "rightOption", "rightControl"};
    static const char *anyEnum[3] = {"anyShift", "anyOption", "anyControl"};

    // Handle Flags
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            if (argv[i][1] == 'o' && i + 1 < argc)
            {
                outputDir = argv[i + 1];
                struct stat st;
                if (stat(outputDir, &st) != 0 || !S_ISDIR(st.st_mode))
                {
                    if (isatty(STDIN_FILENO))
                    {
                        fprintf(stderr, "Directory \"%s\" Doesn't Exist. Create it? [y/N]: ", outputDir);
                        int answer = getchar();
                        if (answer == 'y' || answer == 'Y')
                        {
                            if (mkdir(outputDir, 0755) != 0)
                            {
                                fprintf(stderr, "Error: Couldn't create \"%s\": %s\n", outputDir, strerror(errno));
                                return 1;
                            }
                            fprintf(stderr, "Created Directory \"%s\"\n", outputDir);
                        }
                        else
                        {
                            fprintf(stderr, "Aborted: No Such Directory \"%s\", nothing was written.\n", outputDir);
                            return 1;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "Error: No Such Directory \"%s\"\n", outputDir);
                        return 1;
                    }
                }
                i++;
                continue;
            }
            else if (argv[i][1] == 'o')
            {
                fprintf(stderr, "Error: '-o' needs a directory\n");
                return 1;
            }
            for (int j = 1; j < strlen(argv[i]); j++)
            {
                switch (argv[i][j])
                {
                case 'c':
                    noComment = 1;
                    break;
                case 's':
                    isStrip = 1;
                    break;
                case 'v':
                    printf("%s\n", VERSION);
                    return 0;
                case 'h':
                    printf("macXtract: Decompile installed macOS keyboard layouts into .keylayout files\n"
                           "Usage: macxtract [-scvh] [-o <dir>]\n\n"
                           "-o <dir>  write files to <dir> (default: ./decompiled when no -o flag is given)\n"
                           "-s        strict mode: XML 1.0, control characters stripped\n"
                           "-c        leave out the header comment\n"
                           "-v        print the version\n"
                           "-h        show this help\n\n"
                           "Flags can be combined: macxtract -sc -o layouts\n");
                    return 0;
                default:
                    fprintf(stderr, "Error: unknown flag '-%c'\n", argv[i][j]);
                    return 1;
                }
            }
        }
        else
        {
            fprintf(stderr, "Error: Unexpected Argument \"%s\"\n", argv[i]);
            return 1;
        }
    }

    if (strcmp(outputDir, ".") == 0)
    {
        if (mkdir("decompiled/", 0777) != 0 && errno != EEXIST)
        {
            fprintf(stderr, "Error: %s\n", strerror(errno));
            return 1;
        }
        outputDir = "decompiled";
    }

    CFArrayRef list = TISCreateInputSourceList(NULL, true);
    if (list == NULL)
    {
        fprintf(stderr, "Error: Couldn't read input sources\n");
        return 1;
    }
    CFIndex length = CFArrayGetCount(list);

    char seenIDs[400][256];
    CFTypeRef seenBytes[400];
    int seenCount = 0;
    char appleID[256];
    char localName[256];
    int failureCount = 0;
    for (int i = 0; i < length; i++)
    {
        TISInputSourceRef layout = (TISInputSourceRef)CFArrayGetValueAtIndex(list, i);
        CFTypeRef bytes = (CFDataRef)TISGetInputSourceProperty(layout, kTISPropertyUnicodeKeyLayoutData);
        CFTypeRef cfAppleID = TISGetInputSourceProperty(layout, kTISPropertyInputSourceID);
        CFTypeRef cfLocalName = TISGetInputSourceProperty(layout, kTISPropertyLocalizedName);
        if (bytes == NULL)
            continue;

        CFStringGetCString(cfAppleID, appleID, sizeof(appleID), kCFStringEncodingUTF8);
        CFStringGetCString(cfLocalName, localName, sizeof(localName), kCFStringEncodingUTF8);
        int isDuplicateLayout = 0;
        int copyNumber = 1;
        for (int s = 0; s < seenCount; s++)
            if (strcmp(seenIDs[s], appleID) == 0)
            {
                if (CFEqual(bytes, seenBytes[s]))
                    isDuplicateLayout = 1;
                else
                    copyNumber++;
            }

        if (isDuplicateLayout)
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", outputDir, appleID);
        if (copyNumber > 1)
            snprintf(path + strlen(path), sizeof(path) - strlen(path), "-%i", copyNumber);
        snprintf(path + strlen(path), sizeof(path) - strlen(path), ".keylayout");

        const UInt8 *rawBytes = CFDataGetBytePtr(bytes);
        const UCKeyboardLayout *keyLayout = (const UCKeyboardLayout *)rawBytes;
        if (keyLayout->keyLayoutHeaderFormat != kUCKeyLayoutHeaderFormat)
        {
            fprintf(stderr, "Failed: %s: unknown uchr format\n", appleID);
            failureCount++;
            continue;
        }
        FILE *f = fopen(path, "w");
        if (f == NULL)
        {
            fprintf(stderr, "Failed: %s: %s\n", appleID, strerror(errno));
            failureCount++;
            continue;
        }
        const UCKeyboardTypeHeader *baseKeyboardType = &keyLayout->keyboardTypeList[0];

        // HEADER
        fprintf(f, "<?xml version=\"%s\" encoding=\"UTF-8\"?>\n", isStrip ? "1.0" : "1.1");
        fprintf(f, "<!DOCTYPE keyboard SYSTEM \"file://localhost/System/Library/DTDs/KeyboardLayout.dtd\">\n");

        // MacXTract Header Message
        if (!noComment)
        {
            time_t now = time(NULL);
            struct tm *current = localtime(&now);

            char createdAt[64];
            strftime(createdAt, sizeof(createdAt), "%A %B %d %Y at %H:%M:%S", current);
            fprintf(f, "<!--\n");
            fprintf(f, "\t\t Data Generated by macXtract\n");
            fprintf(f, "\t\t On %s\n", createdAt);
            fprintf(f, "\t\t Version: %s\n", VERSION);
            fprintf(f, "-->\n");
        }

        const UCKeyLayoutFeatureInfo *featureInfo = (const UCKeyLayoutFeatureInfo *)(rawBytes + keyLayout->keyLayoutFeatureInfoOffset);
        fprintf(f, "<keyboard group=\"126\" id=\"%i\" name=\"%s\"", hashString(appleID), localName);
        if (keyLayout->keyLayoutFeatureInfoOffset != 0 && featureInfo->keyLayoutFeatureInfoFormat == 0x2001)
            fprintf(f, " maxout=\"%u\">\n", featureInfo->maxOutputStringLength);
        else
            fprintf(f, ">\n");
        fprintf(f, "\t<layouts>\n");
        for (UInt32 x = 0; x < keyLayout->keyboardTypeCount; x++)
        {
            const UCKeyboardTypeHeader *keyboardType = &keyLayout->keyboardTypeList[x];
            fprintf(f, "\t\t<layout first=\"%u\" last=\"%u\" modifiers=\"%x\" mapSet=\"%x\" />\n", keyboardType->keyboardTypeFirst, keyboardType->keyboardTypeLast, keyboardType->keyModifiersToTableNumOffset, keyboardType->keyToCharTableIndexOffset);
        }
        fprintf(f, "\t</layouts>\n");
        int seenModifiers[keyLayout->keyboardTypeCount];
        for (int j = 0; j < sizeof(seenModifiers) / sizeof(seenModifiers[0]); j++)
            seenModifiers[j] = -1;

        // MODIFIER TABLE
        for (UInt32 layoutCount = 0; layoutCount < keyLayout->keyboardTypeCount; layoutCount++)
        {
            const UCKeyboardTypeHeader *keyboardType = &keyLayout->keyboardTypeList[layoutCount];
            const UCKeyModifiersToTableNum *modifiersTable = (const UCKeyModifiersToTableNum *)(rawBytes + keyboardType->keyModifiersToTableNumOffset);
            const UCKeyToCharTableIndex *charTable = (const UCKeyToCharTableIndex *)(rawBytes + keyboardType->keyToCharTableIndexOffset);
            int modifierLists[charTable->keyToCharTableCount][modifiersTable->modifiersCount];

            // check if its the same default index
            int isRepeat = 0;
            for (int j = 0; j < sizeof(seenModifiers) / sizeof(seenModifiers[0]); j++)
            {
                if (seenModifiers[j] == keyboardType->keyModifiersToTableNumOffset)
                {
                    isRepeat = 1;
                    break;
                }
            }
            // check if it uses the same modifierTable
            if (isRepeat)
                continue;
            seenModifiers[layoutCount] = keyboardType->keyModifiersToTableNumOffset;

            fprintf(f, "\t<modifierMap id=\"%x\" defaultIndex=\"%u\">\n", keyboardType->keyModifiersToTableNumOffset, modifiersTable->defaultTableNum);

            for (int i = 0; i < charTable->keyToCharTableCount; i++)
                for (int j = 0; j < modifiersTable->modifiersCount; j++)
                    modifierLists[i][j] = -1;
            for (int combo = 0; combo < modifiersTable->modifiersCount; combo++)
                if (modifiersTable->tableNum[combo] < charTable->keyToCharTableCount)
                    modifierLists[modifiersTable->tableNum[combo]][combo] = combo;

            for (UInt32 j = 0; j < charTable->keyToCharTableCount; j++)
            {

                // minimize the modifiers.
                int modifierArraySize = sizeof(modifierLists[j]) / sizeof(modifierLists[j][0]);
                for (int combo = 0; combo < modifierArraySize; combo++)
                {
                    int pattern = modifierLists[j][combo];
                    if (pattern == -1)
                    {
                        for (int k = combo; k < modifierArraySize - 1; k++)
                            modifierLists[j][k] = modifierLists[j][k + 1];
                        modifierArraySize--;
                        combo--;
                    }
                }
                if (modifierArraySize == 0)
                    continue;

                int workingValues[7000];
                int workingMasks[7000] = {0};
                int workingUsed[7000] = {0};
                int nextValues[7000] = {0};
                int nextMasks[7000] = {0};
                int primeValues[7000] = {0};
                int primeMasks[7000] = {0};
                int primeCount = 0;
                memcpy(workingValues, modifierLists[j], modifierArraySize * sizeof(int));
                int workingCount = modifierArraySize;

                // fills Prime Values and Prime Masks
                while (workingCount > 0)
                {
                    int nextCount = 0;
                    memset(workingUsed, 0, sizeof(workingUsed));

                    for (int a = 0; a < workingCount - 1; a++)
                        for (int b = a + 1; b < workingCount; b++)
                        {
                            UInt8 c = workingValues[a] ^ workingValues[b];
                            if (workingMasks[a] == workingMasks[b] && c != 0 && (c & (c - 1)) == 0)
                            {
                                workingUsed[a] = 1;
                                workingUsed[b] = 1;
                                int isStored = 0;
                                for (int value = 0; value < nextCount; value++)
                                    if ((workingValues[a] & ~c) == nextValues[value] && (workingMasks[a] | c) == nextMasks[value])
                                    {
                                        isStored = 1;
                                        break;
                                    }
                                if (isStored)
                                    continue;
                                nextValues[nextCount] = workingValues[a] & ~c;
                                nextMasks[nextCount] = workingMasks[a] | c;
                                nextCount++;
                            }
                        }
                    for (int value = 0; value < workingCount; value++)
                        if (workingUsed[value] != 1)
                        {
                            primeValues[primeCount] = workingValues[value];
                            primeMasks[primeCount] = workingMasks[value];
                            primeCount++;
                        }
                    memcpy(workingMasks, nextMasks, sizeof(workingMasks));
                    memcpy(workingValues, nextValues, sizeof(workingValues));
                    workingCount = nextCount;
                }

                // merge primes on any controls
                int workValue[primeCount * primeCount];
                int workMask[primeCount * primeCount];
                int workAny[primeCount * primeCount];
                int workUsed[primeCount * primeCount];
                int workCount = primeCount;

                int nextValue[primeCount * primeCount];
                int nextMask[primeCount * primeCount];
                int nextAny[primeCount * primeCount];

                int outValue[primeCount * primeCount];
                int outMask[primeCount * primeCount];
                int outAny[primeCount * primeCount];
                int outCount = 0;

                int pairs[3][2] = {{2, 32}, {8, 64}, {16, 128}};

                for (int p = 0; p < primeCount; p++)
                {
                    workValue[p] = primeValues[p];
                    workMask[p] = primeMasks[p];
                    workAny[p] = 0;
                }

                while (workCount > 0)
                {
                    int nextCount = 0;
                    memset(workUsed, 0, sizeof(workUsed));
                    int needValueBit = -1;
                    int needMaskBit = -1;

                    for (int a = 0; a < workCount; a++)
                        for (int p = 0; p < 3; p++)
                        {
                            int leftBit = pairs[p][0];
                            int rightBit = pairs[p][1];
                            int pairbits = leftBit | rightBit;

                            if ((workValue[a] & leftBit) && (workMask[a] & rightBit))
                            {
                                needValueBit = rightBit;
                                needMaskBit = leftBit;
                            }
                            else if ((workValue[a] & rightBit) && (workMask[a] & leftBit))
                            {
                                needValueBit = leftBit;
                                needMaskBit = rightBit;
                            }
                            else
                                continue;

                            for (int b = a + 1; b < workCount; b++)
                                if ((workValue[b] & needValueBit) && (workMask[b] & needMaskBit) &&
                                    ((workValue[a] & ~pairbits) == (workValue[b] & ~pairbits)) &&
                                    ((workMask[a] & ~pairbits) == (workMask[b] & ~pairbits)) &&
                                    workAny[a] == workAny[b])
                                {
                                    workUsed[a] = 1;
                                    workUsed[b] = 1;
                                    int isDuplicate = 0;
                                    for (int x = 0; x < nextCount; x++)
                                        if (nextValue[x] == (workValue[a] & ~pairbits) &&
                                            nextMask[x] == (workMask[a] & ~pairbits) &&
                                            nextAny[x] == (workAny[a] | leftBit))
                                            isDuplicate = 1;

                                    if (isDuplicate == 1)
                                        continue;

                                    nextValue[nextCount] = workValue[a] & ~pairbits;
                                    nextMask[nextCount] = workMask[a] & ~pairbits;
                                    nextAny[nextCount] = workAny[a] | leftBit;
                                    nextCount++;
                                }
                        }
                    for (int p = 0; p < workCount; p++)
                    {
                        if (workUsed[p] != 0)
                            continue;

                        outValue[outCount] = workValue[p];
                        outMask[outCount] = workMask[p];
                        outAny[outCount] = workAny[p];
                        outCount++;
                    }

                    memcpy(workValue, nextValue, nextCount * sizeof(int));
                    memcpy(workMask, nextMask, nextCount * sizeof(int));
                    memcpy(workAny, nextAny, nextCount * sizeof(int));
                    workCount = nextCount;
                }

                int combos[modifierArraySize];
                int isCovered[modifierArraySize];
                for (int mod = 0; mod < modifierArraySize; mod++)
                {
                    combos[mod] = modifierLists[j][mod];
                    isCovered[mod] = 0;
                }

                int finalPrimes[outCount];
                int finalPrimeCount = 0;

                // check which prime implicants cover which combos
                int primeCovers[outCount][modifierArraySize];
                for (int x = 0; x < outCount; x++)
                {
                    int prime = outValue[x];
                    int anyBits = 0;
                    for (int p = 0; p < 3; p++)
                        if (outAny[x] & pairs[p][0])
                            anyBits |= pairs[p][0] | pairs[p][1];
                    for (int y = 0; y < modifierArraySize; y++)
                    {
                        int combo = combos[y] & ~(outMask[x] | anyBits);
                        int anyOk = 1;
                        for (int p = 0; p < 3; p++)
                            if ((outAny[x] & pairs[p][0]) && !(combos[y] & (pairs[p][0] | pairs[p][1])))
                                anyOk = 0;
                        if (prime == combo && anyOk)
                            primeCovers[x][y] = 1;
                        else
                            primeCovers[x][y] = 0;
                    }
                }

                int uncoveredCount = modifierArraySize;
                // check if a combo is covered by just one prime implicant
                for (int y = 0; y < modifierArraySize; y++)
                {
                    if (isCovered[y] == 1)
                        continue;

                    int howManyCovered = 0;
                    int primeIndex = 0;
                    for (int z = 0; z < outCount; z++)
                        if (primeCovers[z][y] == true)
                        {
                            howManyCovered++;
                            primeIndex = z;
                        }
                    if (howManyCovered != 1)
                        continue;

                    for (int z = 0; z < modifierArraySize; z++)
                        if (primeCovers[primeIndex][z] == 1 && isCovered[z] == 0)
                        {
                            isCovered[z] = 1;
                            uncoveredCount--;
                        }

                    finalPrimes[finalPrimeCount] = primeIndex;
                    finalPrimeCount++;
                }

                // concatenate remaining primes using greedy search
                while (uncoveredCount > 0)
                {
                    int mostCoveredPrime = 0;
                    int primeToAdd = 0;
                    for (int primeIndex = 0; primeIndex < outCount; primeIndex++)
                    {
                        int foundPrime = 0;
                        for (int finalPrimeindex = 0; finalPrimeindex < finalPrimeCount; finalPrimeindex++)
                            if (primeIndex == finalPrimes[finalPrimeindex])
                            {
                                foundPrime = 1;
                                break;
                            }
                        if (foundPrime)
                            continue;
                        int count = 0;
                        for (int z = 0; z < modifierArraySize; z++)
                            if (primeCovers[primeIndex][z] == 1 && isCovered[z] == 0)
                                count++;
                        if (count > mostCoveredPrime)
                        {
                            mostCoveredPrime = count;
                            primeToAdd = primeIndex;
                        }
                    }
                    for (int z = 0; z < modifierArraySize; z++)
                        if (primeCovers[primeToAdd][z] == 1 && isCovered[z] == 0)
                        {
                            isCovered[z] = 1;
                            uncoveredCount--;
                        }
                    finalPrimes[finalPrimeCount] = primeToAdd;
                    finalPrimeCount++;
                }

                fprintf(f, "\t\t<keyMapSelect mapIndex=\"%u\">\n", j);
                for (int primeIndex = 0; primeIndex < finalPrimeCount; primeIndex++)
                {
                    int prime = outValue[finalPrimes[primeIndex]];
                    int mask = outMask[finalPrimes[primeIndex]];
                    int any = outAny[finalPrimes[primeIndex]];
                    int isHandled = 0;
                    char keys[256] = "";
                    for (int p = 0; p < 3; p++)
                    {
                        int pairsBit = pairs[p][0] | pairs[p][1];
                        if (any & pairs[p][0])
                        {
                            if (keys[0] != '\0')
                                strcat(keys, " ");
                            strcat(keys, anyEnum[p]);
                            isHandled |= pairsBit;
                        }
                        else if ((mask & pairsBit) == pairsBit)
                        {
                            if (keys[0] != '\0')
                                strcat(keys, " ");
                            strcat(keys, anyEnum[p]);
                            strcat(keys, "?");
                            isHandled |= pairsBit;
                        }
                    }
                    for (int bit = 0; bit < 8; bit++)
                    {
                        if (prime & (1 << bit))
                        {
                            if (keys[0] != '\0')
                                strcat(keys, " ");
                            strcat(keys, modifiersEnum[bit]);
                        }
                        if (isHandled & (1 << bit))
                            continue;
                        if (mask & (1 << bit))
                        {
                            if (keys[0] != '\0')
                                strcat(keys, " ");
                            strcat(keys, modifiersEnum[bit]);
                            strcat(keys, "?");
                        }
                    }
                    fprintf(f, "\t\t\t<modifier keys=\"%s\" />\n", keys);
                }
                fprintf(f, "\t\t</keyMapSelect>\n");
            }
            fprintf(f, "\t</modifierMap>\n");
        }

        int seenLayout[keyLayout->keyboardTypeCount];
        for (int j = 0; j < sizeof(seenLayout) / sizeof(seenLayout[0]); j++)
            seenLayout[j] = -1;

        // CHARACTERS TABLE
        for (UInt32 layoutCount = 0; layoutCount < keyLayout->keyboardTypeCount; layoutCount++)
        {
            // check if its the same default index
            const UCKeyboardTypeHeader *keyboardType = &keyLayout->keyboardTypeList[layoutCount];
            const UCKeySequenceDataIndex *sequenceTable = (const UCKeySequenceDataIndex *)(rawBytes + keyboardType->keySequenceDataIndexOffset);
            const UCKeyToCharTableIndex *charTable = (const UCKeyToCharTableIndex *)(rawBytes + keyboardType->keyToCharTableIndexOffset);
            const UCKeyToCharTableIndex *baseCharTable = (const UCKeyToCharTableIndex *)(rawBytes + baseKeyboardType->keyToCharTableIndexOffset);
            const UCKeyOutput *baseKeyOutput = NULL;

            int hasSequenceTable = keyboardType->keySequenceDataIndexOffset != 0;
            int isAlternateTable = layoutCount != 0;
            int isRepeat = 0;
            for (int j = 0; j < sizeof(seenLayout) / sizeof(seenLayout[0]); j++)
                if (seenLayout[j] == keyboardType->keyToCharTableIndexOffset)
                    isRepeat = 1;
            if (isRepeat)
                continue;
            seenLayout[layoutCount] = keyboardType->keyToCharTableIndexOffset;

            fprintf(f, "\t<keyMapSet id=\"%x\">\n", keyboardType->keyToCharTableIndexOffset);
            for (int t = 0; t < charTable->keyToCharTableCount; t++)
            {
                const UCKeyOutput *keyOutput = (const UCKeyOutput *)(rawBytes + charTable->keyToCharTableOffsets[t]);
                int useBase = 0;
                if (isAlternateTable && baseCharTable->keyToCharTableSize <= charTable->keyToCharTableSize && t < baseCharTable->keyToCharTableCount)
                {
                    baseKeyOutput = (const UCKeyOutput *)(rawBytes + baseCharTable->keyToCharTableOffsets[t]);
                    int canUseBase = 1;
                    int diffCount = 0;
                    for (int k = 0; k < baseCharTable->keyToCharTableSize; k++)
                    {
                        if (keyOutput[k] != baseKeyOutput[k])
                            diffCount++;
                        if ((keyOutput[k] == 0xFFFF || keyOutput[k] == 0xFFFE) && (baseKeyOutput[k] != 0xFFFF && baseKeyOutput[k] != 0xFFFE))
                            canUseBase = 0;
                    }
                    if (canUseBase && diffCount > 0)
                        useBase = 1;
                }

                if (useBase)
                    fprintf(f, "\t\t<keyMap index=\"%i\" baseMapSet=\"%x\" baseIndex=\"%i\">\n", t, baseKeyboardType->keyToCharTableIndexOffset, t);
                else
                    fprintf(f, "\t\t<keyMap index=\"%i\">\n", t);

                for (int k = 0; k < charTable->keyToCharTableSize; k++)
                {
                    if (useBase && k < baseCharTable->keyToCharTableSize && keyOutput[k] == baseKeyOutput[k])
                        continue;
                    char output[256] = "";
                    if (keyOutput[k] == 0xFFFF || keyOutput[k] == 0xFFFE)
                        continue;
                    if ((keyOutput[k] & 0xC000) == 0x4000)
                        snprintf(output, sizeof(output), "action=\"a%u\"", keyOutput[k] & 0x3FFF);
                    else
                        checkCharType(keyOutput[k], sequenceTable, hasSequenceTable, output, sizeof(output));

                    fprintf(f, "\t\t\t<key code=\"%i\" %s />\n", k, output);
                }
                fprintf(f, "\t\t</keyMap>\n");
            }
            fprintf(f, "\t</keyMapSet>\n");
        }

        // for action and terminal table
        int hasSequenceTable = baseKeyboardType->keySequenceDataIndexOffset != 0;
        const UCKeySequenceDataIndex *sequenceTable = (const UCKeySequenceDataIndex *)(rawBytes + baseKeyboardType->keySequenceDataIndexOffset);

        // ACTIONS TABLE
        const UCKeyStateRecordsIndex *actionElement = (const UCKeyStateRecordsIndex *)(rawBytes + baseKeyboardType->keyStateRecordsIndexOffset);
        if (actionElement->keyStateRecordsIndexFormat == 0x5001)
        {
            fprintf(f, "\t<actions>\n");
            for (int c = 0; c < actionElement->keyStateRecordCount; c++)
            {
                const UCKeyStateRecord *action = (const UCKeyStateRecord *)(rawBytes + actionElement->keyStateRecordOffsets[c]);
                fprintf(f, "\t\t<action id=\"a%i\">\n", c);
                char output[256] = "";
                char actionWhen[256] = "";
                snprintf(actionWhen, sizeof(actionWhen), "<when state=\"none\"");
                checkCharType(action->stateZeroCharData, sequenceTable, hasSequenceTable, output, sizeof(output));

                if (output[0] != '\0')
                    snprintf(actionWhen + strlen(actionWhen), sizeof(actionWhen) - strlen(actionWhen), " %s", output);
                if (action->stateZeroNextState != 0)
                    snprintf(actionWhen + strlen(actionWhen), sizeof(actionWhen) - strlen(actionWhen), " next=\"%u\"", action->stateZeroNextState);
                fprintf(f, "\t\t\t%s />\n", actionWhen);

                if (action->stateEntryFormat == 1)
                {
                    const UCKeyStateEntryTerminal *entries = (const UCKeyStateEntryTerminal *)&action->stateEntryData[0];
                    for (int e = 0; e < action->stateEntryCount; e++)
                    {
                        char terminalOutput[256] = "";
                        char terminalWhen[256] = "";
                        snprintf(terminalWhen, sizeof(terminalWhen), "<when state=\"%u\"", entries[e].curState);
                        checkCharType(entries[e].charData, sequenceTable, hasSequenceTable, terminalOutput, sizeof(terminalOutput));
                        if (terminalOutput[0] != '\0')
                            snprintf(terminalWhen + strlen(terminalWhen), sizeof(terminalWhen) - strlen(terminalWhen), " %s", terminalOutput);
                        fprintf(f, "\t\t\t%s />\n", terminalWhen);
                    }
                }
                else
                {
                    const UCKeyStateEntryRange *entries = (const UCKeyStateEntryRange *)&action->stateEntryData[0];
                    for (int e = 0; e < action->stateEntryCount; e++)
                    {
                        UCKeyStateEntryRange entry = entries[e];
                        char rangeOutput[256] = "";
                        char whenOutput[256] = "";
                        snprintf(whenOutput + strlen(whenOutput), sizeof(whenOutput) - strlen(whenOutput), "<when state=\"%u\"", entry.curStateStart);
                        if (entry.curStateRange != 0)
                            snprintf(whenOutput + strlen(whenOutput), sizeof(whenOutput) - strlen(whenOutput), " through=\"%u\" multiplier=\"%u\"",
                                     entry.curStateStart + entry.curStateRange, entry.deltaMultiplier);

                        checkCharType(entry.charData, sequenceTable, hasSequenceTable, rangeOutput, sizeof(rangeOutput));
                        if (rangeOutput[0] != '\0')
                            snprintf(whenOutput + strlen(whenOutput), sizeof(whenOutput) - strlen(whenOutput), " %s", rangeOutput);

                        if (entry.nextState != 0)
                            snprintf(whenOutput + strlen(whenOutput), sizeof(whenOutput) - strlen(whenOutput), " next=\"%u\"", entry.nextState);

                        fprintf(f, "\t\t\t%s />\n", whenOutput);
                    }
                }
                fprintf(f, "\t\t</action>\n");
            }
            fprintf(f, "\t</actions>\n");
        }

        // TERMINATORS TABLE
        const UCKeyStateTerminators *terminatorsElemenet = (const UCKeyStateTerminators *)(rawBytes + baseKeyboardType->keyStateTerminatorsOffset);
        if (terminatorsElemenet->keyStateTerminatorsFormat == 0x6001)
        {
            fprintf(f, "\t<terminators>\n");
            for (int t = 0; t < terminatorsElemenet->keyStateTerminatorCount; t++)
            {
                char terminatorOutput[256] = "";
                char terminatorWhen[256] = "";
                snprintf(terminatorWhen, sizeof(terminatorWhen), "<when state=\"%i\"", t + 1);
                checkCharType(terminatorsElemenet->keyStateTerminators[t], sequenceTable, hasSequenceTable, terminatorOutput, sizeof(terminatorOutput));
                if (terminatorOutput[0] != '\0')
                    snprintf(terminatorWhen + strlen(terminatorWhen), sizeof(terminatorWhen) - strlen(terminatorWhen), " %s", terminatorOutput);
                fprintf(f, "\t\t%s />\n", terminatorWhen);
            }
            fprintf(f, "\t</terminators>\n");
        }
        fprintf(f, "</keyboard>\n");

        fclose(f);
        if (copyNumber > 1)
            fprintf(stderr, "Decompiled: %s-%i\n", appleID, copyNumber);
        else
            fprintf(stderr, "Decompiled: %s\n", appleID);

        if (seenCount < 400)
        {
            strcpy(seenIDs[seenCount], appleID);
            seenBytes[seenCount] = bytes;
            seenCount++;
        }
    }

    if (failureCount == 0)
        fprintf(stderr, "Successfully Decompiled All Layouts\n");
    else
    {
        fprintf(stderr, "%i layout(s) failed\n", failureCount);
        return 1;
    }
    return 0;
}
