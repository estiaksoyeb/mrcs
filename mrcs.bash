# bash completion for mrcs

_mrcs_completion() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    
    # Supported mrcs commands
    opts="init commit log diff show restore status current list delete help"

    # Complete the main command
    if [ "${COMP_CWORD}" -eq 1 ] ; then
        COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
        return 0
    fi

    # Subcommand specific completion
    case "${prev}" in
        init|commit|log|diff|show|restore|status|current|delete)
            # Autocomplete with files in the current directory
            COMPREPLY=( $(compgen -f -- "${cur}") )
            return 0
            ;;
        *)
            ;;
    esac
}

complete -F _mrcs_completion mrcs
