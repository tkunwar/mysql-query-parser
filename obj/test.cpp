#include<iostream>
using namespace std;

void modify(int *n){
    cout<<*n<<"\n";
    (*n)++;
    cout<<*n<<"\n";
    n++;
    cout<<*n<<"\n";
    *(n++);
    cout<<*n<<"\n";
}
int main(){
    int num =5;
    modify(&num);
}
