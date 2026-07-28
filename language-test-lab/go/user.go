package main

type User struct {
	DisplayName string
	Active      bool
}

func describeUser(user User) string {
	if user.Active {
		return user.DisplayName + " (active)"
	}

	return user.DisplayName + " (inactive)"
}
