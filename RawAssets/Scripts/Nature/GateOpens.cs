using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class GateOpens : MonoBehaviour
{
    private BoxCollider2D boxCollider;
  //  [SerializeField] private CapsuleCollider2D capsuleCollider;
    [SerializeField] private Animator anim;


    // Start is called before the first frame update
    void Start()
    {        
        boxCollider = GetComponent<BoxCollider2D>();
        anim.SetBool("Idle", true);
      //  boxCollider.isTrigger = false;+
     //   capsuleCollider.isTrigger = true;
    }

    // Update is called once per frame
    void Update()
    {
        
    }

    private void OnTriggerEnter2D(Collider2D collision)
    {
        if(collision.gameObject.tag == "Player")
        {
            Debug.Log("open");
            anim.SetBool("Idle", false);
            anim.SetTrigger("Open");
            anim.SetBool("Opened", true);            
            boxCollider.isTrigger = true;
        //    capsuleCollider.isTrigger = true;
        }

    }

    private void OnTriggerExit2D(Collider2D collision)
    {          
            Debug.Log("closed");
            anim.SetBool("Opened", false);
            anim.SetTrigger("Close");
            anim.SetBool("Idle", true);
     //       boxCollider.isTrigger = false;+
           // capsuleCollider.isTrigger = true;   
        
    }


}
