#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <vector>
#include <string>
#include <future>
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <signal.h>

using namespace std;
#define DEFAULT_BUFLEN 1024
#define DEFAULT_PORT "95"
#define MAX_CLIENT 6

bool endServer = false;
int max_client = 0;
int clientCount = 0;
vector<thread> threads;

struct dirNode *root;

#define pageSize 100

mutex pagemtx;

//global variables
int tableAddress;
vector<int> freePages;
int flag = 0;
vector<string> users;

//check for string parameter is number

bool is_number(const std::string &s)
{
    std::string::const_iterator it = s.begin();
    while (it != s.end() && std::isdigit(*it))
        ++it;
    return !s.empty() && it == s.end();
}

//color coding the console
void changeColor(int desiredColor)
{
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), desiredColor);
}

//structure of every directory node in the tree
struct dirNode
{
    struct dirNode *parent;
    string dirName;
    vector<struct dirNode *> location; //vector to store further directories
    vector<struct fileNode *> files;

    dirNode(string name)
    { //if a new blank directory is being created
        dirName = name;
    }
};

//structure for every file node in tree
struct fileNode
{
    struct dirNode *parent;
    string fileName;
    vector<int> pages; //vector for page pointers
    int lt_len = 0;
    string mode = "";
    int fileOpenCount = 0;
    mutex writeFile_mtx;
};

//struct
struct val
{
    struct dirNode *currentPath;
    struct fileNode *currentFile;
    string mode = "";
};

//adding data to sample file
void getPages()
{
    string myText, temp;
    ifstream ReadFile("Sample.txt");
    getline(ReadFile, myText, '}');

    stringstream str(myText);
    while (getline(str, temp, ',')) //get the pages from the file
    {
        stringstream num(temp);

        int x = 0;
        num >> x;
        freePages.push_back(x);
    }
    ReadFile.close();
}

void writePages()
{
    ofstream WriteFile("Sample.txt", ios::out | ios::in);
    WriteFile.seekp(0); //set the file pointer to begining
    string temp;

    for (int i = 0; i < freePages.size(); i++) //write freed pages into the file
    {
        stringstream strs;
        //check for movement of table
        int pageStore = freePages[1];
        int tableStore = freePages[0];
        while ((tableStore - 1) <= pageStore)
        {
            freePages[0] = freePages[0] + 10;
            tableAddress = freePages[0];
            pageStore = pageStore - pageSize;
        }

        strs << freePages[i];

        if ((i + 1) < freePages.size())
            temp = temp + strs.str() + ",";
        else if ((i + 1) == freePages.size())
        {
            temp = temp + strs.str() + "}"; //ending character
            break;
        }
    }

    WriteFile.write(temp.c_str(), pageSize); //writing till the specified table length
    WriteFile.close();
}

void addtoSampleFile(vector<int> pages, string data, int marker)
{

    int locate; //locate the pointer
    ofstream file;
    int index = 0; //index for the data

    file.open("Sample.txt", ios::out | ios::in);
    string text = data.substr(data.length() - marker, data.length()); //get the length of the text for last page
    int i = 0;
    while (i < pages.size())
    {
        if (index > data.length())
        {
            locate = pages[i] * pageSize;
            file.seekp(locate + 100);         //changed         //set the pointer to the page number * page length + the size of the locater page
            file.write(text.c_str(), marker); //marker for the last page
            break;
        }
        locate = pages[i] * 100; //changed
        file.seekp(locate);
        file.write(data.substr(index, 100).c_str(), 100); //changed
        index += 100;                                     //changed
        i++;
    }

    file.close();
}

void createFile(string name, dirNode *dir, string data)
{
    int count = 0;                        //count for the pages
    int index = data.length() % pageSize; //getting marker for the last page
    struct fileNode *newFile = new fileNode();
    int no_pages = ceil((float)data.length() / pageSize); //get the maximum number of pages
    pagemtx.lock();
    int size = freePages.size(); //getting size of the freePages vector
    newFile->fileName = name;
    newFile->parent = dir;
    dir->files.push_back(newFile);

    //size of the vector increases two, first fill the free pages
    if (freePages.size() > 2)
    {
        while (no_pages > 0 && freePages.size() > 2)
        {
            newFile->pages.push_back(freePages[2]);
            freePages.erase(freePages.begin() + 2); //erase the page used
            no_pages--;                             //subtracting the number of pages used
            count++;
        }
    }

    if (no_pages >= 0 && count >= (size - 2))
    {
        for (int i = 0; i < no_pages; i++)
        {
            newFile->pages.push_back(freePages[1]++);
            freePages[1] = newFile->pages.back() + 1;
            count++;
        }
    }
    pagemtx.unlock();

    if (index == 0)
        index = 100; //changed
    newFile->lt_len = index;
    addtoSampleFile(newFile->pages, data, newFile->lt_len);
}

struct fileNode *openFile(string fileName, struct dirNode *currentPath, struct fileNode *currentFile, SOCKET ClientSocket, string mode)
{

    if (currentFile != NULL)
    {
        string sendStr = "Another file is currently opened.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }

        return currentFile;
    }

    string sendStr;

    bool fileFound = false;
    for (int i = 0; i < currentPath->files.size(); i++)
    {
        if ((currentPath->files[i]->fileName == fileName) && ((currentPath->files[i]->mode == mode) || (currentPath->files[i]->mode == "")))
        {
            currentFile = currentPath->files[i];
            sendStr = "The file has been opened.\n";
            currentPath->files[i]->mode = mode;
            // Echo the buffer back to the sender
            int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            if (iSendResult == SOCKET_ERROR)
            {
                cout << "Send failed with error: " << WSAGetLastError() << endl;
                closesocket(ClientSocket);
                WSACleanup();
            }
            currentPath->files[i]->fileOpenCount++;
            fileFound = true;
            break;
        }
        else if (currentPath->files[i]->fileName == fileName)
        {
            sendStr = "File is already opened in " + currentPath->files[i]->mode + " mode\n";
            int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            if (iSendResult == SOCKET_ERROR)
            {
                cout << "Send failed with error: " << WSAGetLastError() << endl;
                closesocket(ClientSocket);
                WSACleanup();
            }
            fileFound = true;
            currentFile = NULL;
            break;
        }
    }

    if (fileFound == false)
    {
        //changeColor(04);
        string sendStr = "File with this name does not exist in this directory.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        currentFile = NULL;
    }
    return currentFile;
}

void readFile(struct fileNode *currentFile, SOCKET ClientSocket)
{
    if (currentFile == NULL)
    {
        // changeColor(04);
        string sendStr = "No file is currently opened.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return;
    }
    // read text from file
    ifstream file;

    file.open("Sample.txt");
    int readTill = pageSize;
    string sendStr = "";
    for (int page : currentFile->pages)
    {
        if (page == currentFile->pages.back())
        {
            readTill = currentFile->lt_len;
        }
        file.seekg(page * pageSize, ios::beg);
        for (size_t i = 0; i < readTill; i++)
        {
            char c;
            file.get(c);
            sendStr += c;
        }
    }
    sendStr += "\n";

    // Echo the buffer back to the sender
    int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    if (iSendResult == SOCKET_ERROR)
    {
        cout << "Send failed with error: " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
    }
    file.close();
}

void readFromSpecificPosition(int start, int size, struct fileNode *currentFile, SOCKET ClientSocket)
{
    if (currentFile == NULL)
    {
        //changeColor(04);
        string sendStr = "No file is currently opened.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return;
    }
    start = start - 1;
    int end = start + size;
    int filePages = currentFile->pages.size();
    long long int sizeOfFile = (filePages - 1) * pageSize + currentFile->lt_len;

    if (end > sizeOfFile)
    {
        //changeColor(04);
        string sendStr = "File size entered is too large!\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return;
    }
    else
    {

        fstream file;
        file.open("Sample.txt");
        int startIndex = start / pageSize; //get index for vector e.g. 26/pageSize -> 3rd page at 2nd index
        int endIndex = end / pageSize;
        int startPage = currentFile->pages[startIndex];
        int endPage = currentFile->pages[endIndex];
        int startChar = start % pageSize;
        int endChar = end % pageSize;

        string sendStr = "";
        //if reading from single page only
        if ((endIndex - startIndex) == 0)
        {
            int x = startPage * pageSize + startChar;
            //read first page
            file.seekp(x);
            for (size_t i = 0; i < size; i++)
            {
                char c;
                file.get(c);
                sendStr += c;
            }
            sendStr += "\n";

            // Echo the buffer back to the sender
            int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            if (iSendResult == SOCKET_ERROR)
            {
                cout << "Send failed with error: " << WSAGetLastError() << endl;
                closesocket(ClientSocket);
                WSACleanup();
            }

            return;
        }

        //read pages bw first and end
        else if ((endIndex - startIndex) > 0)
        {
            int x = startPage * pageSize + startChar;
            //read first page
            file.seekp(x);
            for (size_t i = 0; i < (pageSize - startChar); i++)
            {
                char c;
                file.get(c);
                sendStr += c;
            }

            for (size_t i = 1; i < (endIndex - startIndex); i++)
            {
                int index = currentFile->pages[startIndex + i];
                file.seekp(index * pageSize);
                for (size_t i = 0; i < pageSize; i++)
                {
                    char c;
                    file.get(c);
                    sendStr += c;
                }
            }

            //read last page
            int y = endPage * pageSize;
            file.seekp(y);
            for (size_t i = 0; i < endChar; i++)
            {
                char c;
                file.get(c);
                sendStr += c;
            }
        }
        sendStr += "\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }

        file.close();
    }
}

struct fileNode *closeFile(struct fileNode *currentFile, SOCKET ClientSocket)
{
    if (currentFile == NULL)
    {
        //changeColor(04);
        string sendStr = "No file is currently opened.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
    }
    else
    {

        // changeColor(10);

        string sendStr = "File closed.\n";
        currentFile->fileOpenCount--;
        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        if (currentFile->fileOpenCount == 0)
            currentFile->mode = "";
        currentFile = NULL;
    }
    return currentFile;
}

void writeToFile(struct fileNode *currentFile, string data, SOCKET ClientSocket)
{
    if (currentFile == NULL)
    {
        // changeColor(04);
        string sendStr = "No file is currently opened.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return;
    }
    fstream myFile;
    myFile.open("Sample.txt", fstream::out | fstream::in);
    currentFile->writeFile_mtx.lock();

    int count = 0;
    int lastPage = currentFile->pages.back();
    vector<int> cur_pages = currentFile->pages;
    vector<int> temp;
    //int marker=0;
    //     myFile.seekg(lastPage * pageSize);

    // char c;
    int index = currentFile->lt_len;
    bool contd = true;

    //start writing to file if last page is not full
    if (index < pageSize)
    {
        myFile.seekg((lastPage * pageSize) + (index)); //go to end of current text -
        string s = data.substr(0, pageSize - index);   // -1
        if (s.length() == data.length())
        {
            contd = false;
        }
        if (s.length() == (pageSize - index)) //check to see if data to be entered will completely fill this page
        {
            myFile << s;
            currentFile->lt_len = 10;

            if (!contd)
            {

                currentFile->writeFile_mtx.unlock();

                return;
            }
        }
        else if (s.length() < (pageSize - index)) //update lt_len of current file
        {
            myFile << s;
            currentFile->lt_len = index + s.length();

            currentFile->writeFile_mtx.unlock();
            return;
        }
    }

    data = data.substr(pageSize - index, data.length());
    int no_pages = ceil((float)data.length() / pageSize);
    index = data.length() % pageSize;
    pagemtx.lock();
    if (freePages.size() > 2)
    {
        while (no_pages > 0 && freePages.size() > 2)
        {
            cur_pages.push_back(freePages[2]);
            temp.push_back(freePages[2]);
            freePages.erase(freePages.begin() + 2); //erase the page used
            no_pages--;                             //subtracting the number of pages used
            count++;
        }
    }

    if (no_pages >= 0 && count >= (freePages.size() - 2))
    {
        for (int i = 0; i < no_pages; i++)
        {

            cur_pages.push_back(freePages[1]);
            temp.push_back(freePages[1]++);
            count++;
        }
    }
    pagemtx.unlock();
    currentFile->pages = cur_pages;

    if (index == 0)
        index = 100;
    currentFile->lt_len = index;
    currentFile->writeFile_mtx.unlock();

    addtoSampleFile(temp, data, index);
    myFile.close();
}

void truncateFile(int maxSize, struct fileNode *currentFile, SOCKET ClientSocket)
{
    if (currentFile == NULL)
    {
        //changeColor(04);
        string sendStr = "No file is currently opened.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return;
    }
    string sendStr = "The file being truncated: " + currentFile->fileName + "\n";
    // changeColor(10);
    int position = maxSize;                  //truncate from this character onwards
    int pageNum = (position - 1) / pageSize; //to be truncated from this page onwards
    int filePagesSize = currentFile->pages.size();
    //check length of pages vector, if less give error on position value
    if (filePagesSize < pageNum)
    {
        //changeColor(04);
        sendStr += "Size given is too large. Please try again.\n";
    }
    else
    {
        //push from pageNum to end of vector to free positions wala
        int endPosition = position % pageSize; // iss character pe semi colon daalna hai
        if (endPosition == 0)
            endPosition = pageSize;
        currentFile->lt_len = endPosition;
        for (int i = pageNum + 1; i < filePagesSize; i++)
        {
            pagemtx.lock();
            freePages.push_back(currentFile->pages[i]);
            pagemtx.unlock();
        }
        currentFile->pages.resize(pageNum + 1);
    }

    // Echo the buffer back to the sender
    int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    if (iSendResult == SOCKET_ERROR)
    {
        cout << "Send failed with error: " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
    }
}

//creating directory in currentpath directory
struct dirNode *createDir(string dirName, struct dirNode *currentPath)
{
    struct dirNode *node = new dirNode(dirName);
    node->parent = currentPath;
    if (currentPath != NULL)
        currentPath->location.push_back(node);

    return currentPath;
}

// printing the path from root till the passed node
void printAbsPath(struct dirNode *temp, SOCKET ClientSocket)
{
    string absPath = "";
    while (temp->parent != NULL)
    {
        absPath = temp->dirName + "/" + absPath;
        temp = temp->parent;
    }
    absPath = "root/" + absPath;
    string sendStr = absPath + ": ";

    // Echo the buffer back to the sender
    int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    if (iSendResult == SOCKET_ERROR)
    {
        cout << "Send failed with error: " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
    }
    return;
}

//listing the current directory's contents
void ls(struct dirNode *currentPath, SOCKET ClientSocket)
{
    string sendStr = "";
    for (int i = 0; i < currentPath->location.size(); i++)
    {
        sendStr += currentPath->location[i]->dirName + "   ";
    }
    for (int i = 0; i < currentPath->files.size(); i++)
    {
        sendStr += currentPath->files[i]->fileName + ".txt   ";
    }
    sendStr += "\n";

    // Echo the buffer back to the sender
    int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    if (iSendResult == SOCKET_ERROR)
    {
        cout << "Send failed with error: " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
    }
}

//moving to any given location
struct dirNode *cdPath(dirNode *currentPath, string name, struct dirNode *root, SOCKET ClientSocket)
{
    name = name + "/";
    stringstream ss(name);
    string dirName = "";
    bool dirFound = false;
    getline(ss, dirName, '/');
    if (dirName == "")
    {
        dirFound = true;
        return currentPath;
    }

    if (dirName == "root")
    {
        currentPath = root;
        getline(ss, dirName, '/');
        if (dirName == "")
        {
            dirFound = true;
            return currentPath;
        }
    }
    string sendStr;

    for (int i = 0; i < currentPath->location.size(); i++)
    {
        if (currentPath->location[i]->dirName == dirName)
        {
            getline(ss, name);
            currentPath = cdPath(currentPath->location[i], name, root, ClientSocket);
            dirFound = true;
        }
    }

    changeColor(04);
    if (dirFound == false)
    {
        sendStr = "The said directory does not exist. Going to the last available directory.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
    }
    return currentPath;
}

//moving up the hierarchy
struct dirNode *cdBack(struct dirNode *currentPath, SOCKET ClientSocket)
{
    if (currentPath->parent != NULL)
    {
        currentPath = currentPath->parent;
    }
    else
    {
        //changeColor(04);
        string sendStr = "You have reached the root directory.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
    }
    return currentPath;
}

//calculating digits for serialization
int countDigit(long long n)
{
    int count = 0;
    while (n != 0)
    {
        n = n / 10;
        ++count;
    }
    return count;
}

//writing the entire tree into file with serialization
int serialize(struct dirNode *nodeNow, int seekcount)
{
    ofstream file;

    file.open("Sample.txt", ios::out | ios::in);

    file.seekp(seekcount);
    if (nodeNow->parent != NULL)
    {
        file << nodeNow->dirName << ')';
        seekcount += nodeNow->dirName.length() + 1; //move seekcount forward dirName and )
    }

    for (int x = 0; x < nodeNow->files.size(); x++)
    {
        file << nodeNow->files[x]->fileName << ":" << nodeNow->files[x]->lt_len << ":";
        seekcount += nodeNow->files[x]->fileName.length() + 1 + countDigit(nodeNow->files[x]->lt_len) + 1;
        for (int i = 0; i < nodeNow->files[x]->pages.size(); i++)
        {
            file << nodeNow->files[x]->pages[i] << ":";
            seekcount += countDigit(nodeNow->files[x]->pages[i]) + 1;
        }
        file << ")";
        seekcount++;
    }

    if (nodeNow->location.empty())
    { //base case for recursion
        return seekcount;
    }
    else
    {
        file << "|";
        seekcount++;
        file.close();

        for (int i = 0; i < nodeNow->location.size(); i++)
        {
            seekcount = serialize(nodeNow->location[i], seekcount);

            file.open("Sample.txt", ios::out | ios::in);
            file.seekp(seekcount);
            file << "]";
            seekcount++;
            file.close();
        }
    }

    file.open("Sample.txt", ios::out | ios::in);
    file.seekp(seekcount);
    file << "[";
    file.close();

    return seekcount;
}

//displaying the entire tree
void memoryMap(struct dirNode *nodeNow, SOCKET ClientSocket)
{
    string sendStr = "";
    int iSendResult;
    changeColor(15);
    if (nodeNow->parent != NULL)
    {
        struct dirNode *temp = nodeNow;
        int x = 0;
        while (temp->parent != NULL)
        {
            temp = temp->parent;
            x++;
        }

        for (int r = 1; r < x; r++)
        {
            sendStr = "    ";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }
        sendStr += "|> " + nodeNow->dirName + ":   ";
        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    }
    else
    {
        sendStr = "root/: ";
        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    }
    changeColor(10);

    for (int x = 0; x < nodeNow->files.size(); x++)
    {
        sendStr = nodeNow->files[x]->fileName + ".txt  ";
        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    }

    if (nodeNow->location.empty())
    { //base case for recursion
        sendStr = "\n";
        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        return;
    }
    else
    {
        for (int x = 0; x < nodeNow->location.size(); x++)
        {
            sendStr = nodeNow->location[x]->dirName + "  ";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }
        changeColor(15);
        sendStr = "\n";
        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);

        for (int i = 0; i < nodeNow->location.size(); i++)
        {
            memoryMap(nodeNow->location[i], ClientSocket);
        }
    }
    return;
}

//deserializing the tree from samplefile
struct dirNode *deser(int seekval, struct dirNode *currentPath)
{ //start with root node
    ifstream file;
    string str, dirval, tempval, temp, dfs;
    file.open("Sample.txt", ios::out | ios::in);
    if (!file.is_open())
    {
        cout << "Error opening the file!" << endl;
    }
    file.seekg(seekval);
    getline(file, str, '[');
    str = "root)" + str;
    stringstream str1(str);

    while (getline(str1, dfs, ']'))
    {
        stringstream str2(dfs);
        while (getline(str2, dirval, '|'))
        {

            stringstream str3(dirval);
            if (getline(str3, tempval, ')'))
            {
                if (tempval != "root")
                {
                    currentPath = createDir(tempval, currentPath);
                    currentPath = currentPath->location[currentPath->location.size() - 1];
                }
            }
            while (getline(str3, tempval, ')'))
            {
                stringstream str4(tempval);
                if (getline(str4, temp, ':'))
                {
                    struct fileNode *newFile = new fileNode();
                    newFile->fileName = temp;
                    currentPath->files.push_back(newFile);
                    getline(str4, temp, ':');
                    newFile->lt_len = stoi(temp);
                    while (getline(str4, temp, ':'))
                    {
                        newFile->pages.push_back(stoi(temp));
                    }
                }
            }
        }
        if (currentPath->parent != NULL)
            currentPath = currentPath->parent;
    }
    file.close();
    return currentPath;
}

//moving given file to given location
void moveFile(vector<string> command, struct dirNode *filePath, struct dirNode *root, SOCKET ClientSocket)
{
    //Tokenizing file source w.r.t /
    string srcPath, srcFile;
    vector<string> sourcePath;
    bool fileFound = false;
    struct dirNode *destPath;
    struct dirNode *currPath = filePath;
    int fileloc; //location of file in file vector to remove it

    // reversing for filePath
    reverse(command[1].begin(), command[1].end());

    //creating stringstream to getfilename
    stringstream str(command[1]);

    getline(str, srcFile, '/'); //source file in reverse
    getline(str, srcPath);      //source path in reverse

    //reversing to get correct path and name
    reverse(srcFile.begin(), srcFile.end());
    reverse(srcPath.begin(), srcPath.end());

    //finding filePath
    filePath = cdPath(filePath, srcPath, root, ClientSocket);

    //stating found location
    // changeColor(10);
    string sendStr = "The source directory found for the file: " + filePath->dirName + "\n";

    // Echo the buffer back to the sender
    int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    if (iSendResult == SOCKET_ERROR)
    {
        cout << "Send failed with error: " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
    }

    //search for file in given path
    for (int x = 0; x < filePath->files.size(); x++)
    {
        if (filePath->files[x]->fileName == srcFile)
        {
            fileFound = true;
            fileloc = x;
        }
    }
    if (fileFound == false)
    {
        // changeColor(04);
        sendStr = "The file being moved doesnot exist.\n";
        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }

        return;
    }

    //searching for destination path
    if (command[2] == "current")
    {

        sendStr = "Directory where file is being moved: " + currPath->dirName + "\n";

        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        destPath = currPath;
    }
    else
    {
        destPath = cdPath(currPath, command[2], root, ClientSocket);
        //changeColor(10);
        sendStr = "Directory where file is being moved: " + destPath->dirName + "\n";

        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
    }
    bool filecheck = false;
    //checking if same name file already exists
    for (int x = 0; x < destPath->files.size(); x++)
    {
        if (destPath->files[x]->fileName == srcFile)
        {
            filecheck = true;
        }
    }

    if (filecheck == true)
    {
        // changeColor(04);
        sendStr = "The directory already has a file with the same name. Try again.\n";

        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return;
    }
    //moving the file
    destPath->files.push_back(filePath->files[fileloc]);
    filePath->files.erase(filePath->files.begin() + fileloc);
    //changeColor(10);
    sendStr = "File moved sucessfully.\n";

    iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    if (iSendResult == SOCKET_ERROR)
    {
        cout << "Send failed with error: " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
    }

    return;
}

struct fileNode *deleteFile(struct dirNode *currentPath, struct fileNode *currentFile, SOCKET ClientSocket)
{
    string sendStr;
    if (currentFile == NULL)
    {
        // changeColor(04);
        sendStr = "No file is currently selected.\n";

        // Echo the buffer back to the sender
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return currentFile;
    }

    if (currentFile->fileOpenCount > 1)
    {
        sendStr = currentFile->fileName + ".txt file is opened by another user\n";
        int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        return currentFile;
    }
    // changeColor(04);
    sendStr = "File being deleted: " + currentFile->fileName + "\n";
    int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);

    //freeing the files pages
    for (size_t i = 0; i < currentFile->pages.size(); i++)
    {
        pagemtx.lock();
        freePages.push_back(currentFile->pages[i]);
        pagemtx.unlock();
    }

    //remove element from location vector AND update tree???
    for (int i = 0; i < currentPath->files.size(); i++)
    {
        if (currentPath->files[i]->fileName == currentFile->fileName)
        {
            currentPath->files.erase(currentPath->files.begin() + i);
            break;
        }
    }
    currentFile = NULL;

    sendStr = "The file has been deleted.\n";
    iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);

    // Echo the buffer back to the sender
    if (iSendResult == SOCKET_ERROR)
    {
        cout << "Send failed with error: " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
    }
    return currentFile;
}

//the main function which keeps running until exited
struct val mainFunction(struct val vals, string input, SOCKET ClientSocket)
{
    vector<string> command;
    //string input;
    struct dirNode *currentPath = vals.currentPath;
    struct fileNode *currentFile = vals.currentFile;
    string mode = vals.mode;
    bool file = true;
    bool dir = true;
    string sendStr;
    int iSendResult{};

    printAbsPath(currentPath, ClientSocket);
    int parameter;

    //getline(cin, input);
    // stringstream class check1
    stringstream str(input);

    string intermediate;

    // Tokenizing w.r.t. space ' '
    while (getline(str, intermediate, ' '))
    {
        command.push_back(intermediate);
        if (intermediate == "createFile") //push data part of these commands to 3rd element of vector instead of parsing at space
        {
            getline(str, intermediate, ' ');

            command.push_back(intermediate);
            int start = command[0].length() + command[1].length() + 3;
            if (start > input.length())
                break;
            else
                command.push_back(input.substr(start, (input.size() - 2 - command[0].length() - command[1].length() - 2)));
            break;
        }
        else if (intermediate == "writeFile") //push data part of these commands to 3rd element of vector instead of parsing at space
        {
            int start = command[0].length() + 2;
            if (start > input.length())
                break;
            else
                command.push_back(input.substr(start, (input.length() - command[0].length() - 3)));
            break;
        }
    }

    if (mode == "rf")
    {
        if (command[0] == "readFile")
        { //if user wishes to create directory
            if (command.size() != 1)
            {
                //changeColor(04);
                sendStr = "Parameters invalid.\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
            else
            {
                readFile(currentFile, ClientSocket);
            }
        }

        else if (command[0] == "readFromSpecificPosition")
        { //if user wishes to read specific length of file
            if (command.size() != 3 || !is_number(command[1]) || !is_number(command[2]))
            {
                // changeColor(04);
                sendStr = "Parameters invalid.\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
            else if (stoi(command[1]) <= 0)
            {
                sendStr = "Enter length starting from 1 or greater\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
            else
            {
                readFromSpecificPosition(stoi(command[1]), stoi(command[2]), currentFile, ClientSocket);
            }
        }

        else if ((command[0] == "writeFile") || (command[0] == "deleteFile") || (command[0] == "truncateFile"))
        {
            sendStr = "Command denied: the file is opened in read mode. Open file in write mode to perform command.\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }

        else if (command[0] == "closeFile")
        { //if user wishes to create directory
            if (command.size() != 1)
            {
                //changeColor(04);
                sendStr = "Parameters invalid.\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
            else
            {
                currentFile = closeFile(currentFile, ClientSocket);
                mode = "";
            }
        }

        else
        {
            sendStr = "Incorrect command. File opened in read mode.\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }

        flag = 0;
        vals.currentPath = currentPath;
        vals.currentFile = currentFile;
        vals.mode = mode;
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return vals;
    }

    else if (mode == "wf")
    {
        if (command[0] == "writeFile")
        { //if user wishes to create directory
            if (command.size() != 2)
            {
                //changeColor(04);
                sendStr = "Parameters invalid.\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
            else
            {
                writeToFile(currentFile, command[1], ClientSocket);
            }
        }

        else if (command[0] == "truncateFile")
        { //if user wishes to create directory
            if (command.size() != 2 || !is_number(command[1]))
            {
                // changeColor(04);
                sendStr = "Parameters invalid.\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
            else
            {
                truncateFile(stoi(command[1]), currentFile, ClientSocket);
            }
        }

        else if (command[0] == "deleteFile")
        { //if user wishes to create directory
            if (command.size() != 1)
            {
                // changeColor(04);
                sendStr = "Parameters invalid.\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
            else
            {
                currentFile = deleteFile(currentPath, currentFile, ClientSocket);
            }
        }

        else if ((command[0] == "readFile") || (command[0] == "readFromSpecificPosition"))
        {
            sendStr = "Command denied: the file is opened in write mode. Open file in read mode to perform command.\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }

        else if (command[0] == "closeFile")
        { //if user wishes to create directory
            if (command.size() != 1)
            {
                //changeColor(04);
                sendStr = "Parameters invalid.\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
            else
            {
                currentFile = closeFile(currentFile, ClientSocket);
                mode = "";
            }
        }

        else
        {
            sendStr = "Incorrect command. File opened in write mode.\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }

        flag = 0;
        vals.currentPath = currentPath;
        vals.currentFile = currentFile;
        vals.mode = mode;
        if (iSendResult == SOCKET_ERROR)
        {
            cout << "Send failed with error: " << WSAGetLastError() << endl;
            closesocket(ClientSocket);
            WSACleanup();
        }
        return vals;
    }

    if (command[0] == "createDir")
    { //if user wishes to create directory
        if (command.size() != 2)
        {
            // changeColor(04);
            sendStr = "Parameters invalid\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }
        else
        {
            for (int i = 0; i < currentPath->location.size(); i++)
            {
                if (command[1] == currentPath->location[i]->dirName)
                {
                    dir = false;
                    //changeColor(04);
                    sendStr = "Directory already exists. Write a different directory name.\n";
                    iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
                }
            }
            if (dir)
            {
                currentPath = createDir(command[1], currentPath);
                //changeColor(10);
                sendStr = "Directory created.\n";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
        }
    }

    if (command[0] == "createFile")
    { //if user wishes to create file
        if (command.size() != 3)
        {
            //changeColor(04);
            sendStr = "Parameters invalid\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }
        else
        {
            if (vals.currentFile != NULL)
            {
                closeFile(vals.currentFile, ClientSocket);
            }

            for (int i = 0; i < currentPath->files.size(); i++)
            {
                if (command[1] == currentPath->files[i]->fileName)
                {
                    file = false;

                    sendStr = "File already exists. Write a different file name.\n";
                    iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
                }
            }
            if (file)
            {
                createFile(command[1], currentPath, command[2]);
                //changeColor(10);
                sendStr = "File created.";
                iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            }
        }
    }

    if (command[0] == "openFile")
    { //if user wishes to create directory

        if (command.size() < 3)
        {
            //changeColor(04);
            sendStr = "Parameters invalid.\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }
        else
        {
            if (command[2] == "rf")
            {

                currentFile = openFile(command[1], currentPath, currentFile, ClientSocket, command[2]);
                if (currentFile != NULL)
                    mode = "rf";
            }
            if (command[2] == "wf")
            {
                currentFile = openFile(command[1], currentPath, currentFile, ClientSocket, command[2]);
                if (currentFile != NULL)
                    mode = "wf";
            }
        }
    }

    if (command[0] == "closeFile")
    { //if user wishes to create directory
        if (command.size() != 1)
        {
            //changeColor(04);
            sendStr = "Parameters invalid.\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }
        else
        {
            currentFile = closeFile(currentFile, ClientSocket);
            mode = "";
        }
    }

    else if (command[0] == "cd")
    { //if user wishes to go forward or backward in directory
        if (command.size() != 2)
        {
            // changeColor(04);
            sendStr = "Parameters invalid.\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }
        else if (command[1] == "~")
        {
            // changeColor(10);
            currentPath = cdBack(currentPath, ClientSocket);
        }
        else
        {
            // changeColor(10);
            currentPath = cdPath(currentPath, command[1], root, ClientSocket);
        }
    }

    else if (command[0] == "moveFile")
    {
        if (command.size() != 3)
        {
            // changeColor(04);
            sendStr = "Parameters invalid.\n";
            iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        }
        else
        {
            //changeColor(10);
            moveFile(command, currentPath, root, ClientSocket);
        }
    }

    else if (command[0] == "memoryMap")
    {
        sendStr = "\n The entire file system has the following heirarchy: \n \n";
        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
        memoryMap(root, ClientSocket);
        sendStr = "\n";
        iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
    }

    else if (command[0] == "ls")
    {
        //   changeColor(10);
        ls(currentPath, ClientSocket);
    }

    else if (command[0] == "exit")
    {
        flag = 1;
        return vals;
    }

    flag = 0;
    vals.currentPath = currentPath;
    vals.currentFile = currentFile;
    vals.mode = mode;
    if (iSendResult == SOCKET_ERROR)
    {
        cout << "Send failed with error: " << WSAGetLastError() << endl;
        closesocket(ClientSocket);
        WSACleanup();
    }
    return vals;
}

void closeServer()
{
    cout << "Server closing...." << endl;
    int temp = serialize(root, tableAddress * pageSize);
    writePages();
}

void sigintHandler(int sig_num)
{
    signal(SIGINT, sigintHandler);
    if (clientCount == 0)
    {
        for (auto &t : threads)
            t.join();

        closeServer();
        exit(sig_num);
    }
    else
    {
        if (clientCount == 1)
            cout << clientCount << " Client connected" << endl;
        else
            cout << clientCount << " Clients connected" << endl;
    }
}

void threadFunc(SOCKET ClientSocket)
{
    int len;
    struct val vals;
    vals.currentPath = root;
    vals.currentFile = NULL;

    if (ClientSocket == INVALID_SOCKET)
    {
        cout << "Accept failed with error : " << WSAGetLastError() << endl;

        WSACleanup();
    }

    std::thread::id this_id = std::this_thread::get_id();

    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;

    //get username first
    iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
    recvbuf[iResult] = '\0';
    string username = recvbuf;
    users.push_back(username);
    cout << "Connection established with " << username << endl;
    clientCount++;
    cout << "User(s) connected at the moment: ";
    for (size_t i = 0; i < users.size(); i++)
    {
        cout << users[i] << ", ";
    }
    cout << endl;

    do
    { //get commands from this thread into the recvbuf on loop until exit command is called

        memset(recvbuf, 0, sizeof(recvbuf));
        string command = "";
        while (true)
        {

            iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
            if (iResult > 0)
            {
                recvbuf[iResult] = '\0';
                command = command + recvbuf;

                if (recvbuf[iResult - 1] == ';')
                {
                    cout << "Command: " << command << endl;
                    break;
                }

                else
                {
                    break;
                }
            }
        }

        if (command[command.length() - 1] != ';')
        {
            string sendStr = "Syntax Error. Don't Forget that semi-colon at the end#";
            int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            if (iSendResult == SOCKET_ERROR)
            {
                cout << "Send failed with error: " << WSAGetLastError() << endl;
                closesocket(ClientSocket);
                WSACleanup();
            }
        }
        else if (command.size() > 0 && command[command.length() - 1] == ';')
        {
            command = command.substr(0, command.size() - 1);
            //send command to mainFunc for execution
            vals = mainFunction(vals, command, ClientSocket);
            string sendStr = "#";
            int iSendResult = send(ClientSocket, sendStr.c_str(), (int)sendStr.length(), 0);
            if (iSendResult == SOCKET_ERROR)
            {
                cout << "Send failed with error: " << WSAGetLastError() << endl;
                closesocket(ClientSocket);
                WSACleanup();
            }
            if (command == "exit")
            {
                cout << "Connection closing with user: " << username << endl;
                clientCount--;
                //remove this user from current list of users
                users.erase(remove(users.begin(), users.end(), username), users.end());

                break;
            }
        }

    } while (iResult > 0);

    closesocket(ClientSocket);
    return;
    // cleanup
}

int main()
{

    //code for directory
    //take input from user number of threads
    ifstream file;
    file.open("Sample.txt");
    signal(SIGINT, sigintHandler);
    if (!file.is_open())
    {
        cout << "Error opening the file!" << endl;
    }
    getPages();
    file.close();
    tableAddress = freePages[0];
    root = new dirNode("root"); //the base directory
    root->parent = NULL;
    root = deser(tableAddress * pageSize, root);

    //code for sockets
    // DWORD thread;
    cout << "Server started! Listening for connections..." << endl;
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;

    struct addrinfo *result = NULL;
    struct addrinfo hints;

    int iSendResult;
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0)
    {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for connecting to server
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET)
    {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Setup the TCP listening socket
    iResult = ::bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR)
    {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR)
    {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    vector<thread> threads;

    // Accept a client socket
    //LOOP TO ACCEPT MULTIPLE CLIENTS
    // while (true) {

    for (size_t i = 0; i < MAX_CLIENT; i++) //ACCEPT 5 CLIENTS
    {

        SOCKET ClientSocket;
        ClientSocket = accept(ListenSocket, NULL, NULL);

        threads.push_back(thread(threadFunc, ClientSocket));
    }
    // No longer need server socket
    closesocket(ListenSocket);

    //JOIN THREADS
    for (auto &t : threads)
        t.join();

    //ending code
    closeServer();
    WSACleanup();
    // system("PAUSE");
    return 0;
}