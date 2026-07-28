package main

import "fmt"

func main() {
	user := User{
		DisplayName: "Mnemosyne",
		Active:      true,
	}

	fmt.Println(describeUser(user))
}
